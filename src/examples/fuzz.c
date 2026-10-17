/* fuzz: a system-call fuzzer for the kernel-v2 kernel (M4).

   Proves isolation: a user program making random and adversarial
   system calls must never crash or hang the kernel.

   Driven by scripts/fuzz.sh.  In "parent" mode it re-execs itself in
   "child" mode once per seed; each child makes many random calls with
   hostile arguments, then exits.  The parent waits for each child,
   counts the survivors, and then exercises the kernel itself (create
   and read back a file) to show it is still healthy.

   Usage:  fuzz P <seed> <nseeds> <iters>     parent (driver)
           fuzz C <seed> <iters>              child (one fuzzing run) */

#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

/* Addresses that must never be dereferenced by the kernel. */
static void *const BAD_PTRS[] = {
  (void *) 0, (void *) 1, (void *) 0x1234, (void *) 0x08048000 - 1,
  (void *) 0xbffffffc, (void *) 0xc0000000, (void *) 0xc0000000 + 0x1000,
  (void *) 0xffffffff, (void *) 0xdeadbeef,
};
#define NBAD (sizeof BAD_PTRS / sizeof *BAD_PTRS)

/* A page straddling the user/kernel boundary: the first bytes are
   valid, the rest is not. */
static char boundary_probe[1];

static void *
rand_ptr (void)
{
  switch (random_ulong () % 4)
    {
    case 0: return BAD_PTRS[random_ulong () % NBAD];
    case 1: return (void *) (0xc0000000 - (random_ulong () % 8));  /* near PHYS_BASE */
    case 2: return boundary_probe;
    default: return (void *) (random_ulong () & 0xfffffff0);
    }
}

static int
rand_fd (void)
{
  /* A mix of the standard descriptors, plausible ones and junk. */
  switch (random_ulong () % 4)
    {
    case 0: return random_ulong () % 3;
    case 1: return random_ulong () % 32;
    case 2: return -(int) (random_ulong () % 4);
    default: return (int) random_ulong ();
    }
}

/* Like rand_fd(), but never 0 or 1, so a survivor's read()/write()
   can't block on the console (which would be the fuzzer's fault, not
   the kernel's).  None of these are open in the fuzzer, so the kernel
   returns -1. */
static int
rand_noncon_fd (void)
{
  switch (random_ulong () % 3)
    {
    case 0: return 2 + (int) (random_ulong () % 1024);
    case 1: return -(int) (1 + random_ulong () % 4);
    default: return (int) (random_ulong () | 0x40000000);
    }
}

/* Makes one random call that must NOT kill OR block the caller.  It
   uses valid buffers and hostile descriptors, sizes and values, but
   is non-blocking by construction: it never installs a console- or
   pipe-backed descriptor that a later read could block on, and never
   over-fills its own pipe.  A well-behaved kernel returns -1 for each
   bad descriptor without panicking, letting a survivor child run
   thousands of calls in a row. */
static void
safe_call (void)
{
  static char buf[64];
  int fds[2];

  switch (random_ulong () % 14)
    {
    case 0: filesize (rand_fd ()); break;
    case 1: seek (rand_fd (), random_ulong ()); break;
    case 2: tell (rand_fd ()); break;
    case 3: close (rand_noncon_fd ()); break;  /* never close 0/1 */
    case 4: read (rand_noncon_fd (), buf, random_ulong () % sizeof buf); break;
    case 5: write (rand_noncon_fd (), buf, random_ulong () % sizeof buf); break;
    case 6: write (STDOUT_FILENO, buf, random_ulong () % 4); break;
    case 7: wait ((pid_t) random_ulong ()); break;
    case 8: read (STDIN_FILENO, buf, 0); break;   /* size 0: returns at once */
    case 9: filesize ((int) random_ulong ()); break;
    case 10: dup2 (rand_noncon_fd (), (int) (random_ulong () % 2048)); break;
    case 11: dup2 (rand_fd (), -(int) (random_ulong () % 4)); break;  /* bad newfd */
    default:
      /* Open a pipe and close both ends at once.  Never read our own
         pipe (EOF or block) and never write more than it holds. */
      if (pipe (fds))
        {
          write (fds[1], buf, random_ulong () % 32);
          close (fds[0]);
          close (fds[1]);
        }
    }
}

/* Makes one call that is EXPECTED to make the kernel kill this
   process (a bad pointer or an unknown system call).  Isolation means
   the kernel kills only us, never itself. */
static void
fatal_call (void)
{
  switch (random_ulong () % 7)
    {
    case 0: create (rand_ptr (), random_ulong () % 4096); break;
    case 1: open (rand_ptr ()); break;
    case 2: read (rand_fd (), rand_ptr (), 1 + random_ulong () % 8192); break;
    case 3: write (STDOUT_FILENO, rand_ptr (), 1 + random_ulong () % 8192); break;
    case 4: exec (rand_ptr ()); break;
    case 5: pipe (rand_ptr ()); break;
    default:
      {
        int nr = 100 + (int) (random_ulong () % 50);
        /* Push three dummy args, then NR last so it lands on top of
           the stack as the system call number.  An out-of-range
           number must make the kernel kill us, not run call 0. */
        asm volatile ("pushl $0; pushl $0; pushl $0; pushl %0;"
                      "int $0x30; addl $16, %%esp"
                      : : "r" (nr) : "eax", "memory");
      }
    }
}

/* Survivor child: ITERS non-fatal calls, then a clean exit.  If it
   returns, it really made ITERS system calls. */
static int
survivor (unsigned seed, int iters)
{
  int i;
  random_init (seed);
  for (i = 0; i < iters; i++)
    safe_call ();
  return (int) (seed % 128);
}

/* Bomb child: makes calls with bad pointers until the kernel kills
   it.  Should never return. */
static int
bomb (unsigned seed, int iters)
{
  int i;
  random_init (seed);
  for (i = 0; i < iters; i++)
    fatal_call ();
  return 42;            /* Only reached if NO bad pointer was caught. */
}

/* Runs NSEEDS survivor children (each proving ITERS calls) and NSEEDS
   bomb children (each proving the kill path), then checks the kernel
   still works.  No child can hang the kernel without timing the whole
   run out, so reaching the summary means no hang. */
static int
parent (unsigned seed0, int nseeds, int iters)
{
  int survived = 0, killed = 0, leaked = 0, s, fd;
  char cmd[64], buf[16];

  for (s = 0; s < nseeds; s++)
    {
      pid_t pid;
      snprintf (cmd, sizeof cmd, "fuzz S %u %d", seed0 + s, iters);
      pid = exec (cmd);
      if (pid != PID_ERROR && wait (pid) == (int) ((seed0 + s) % 128))
        survived++;
    }
  for (s = 0; s < nseeds; s++)
    {
      pid_t pid;
      snprintf (cmd, sizeof cmd, "fuzz B %u %d", seed0 + s, iters);
      pid = exec (cmd);
      if (pid == PID_ERROR)
        continue;
      if (wait (pid) == -1)
        killed++;              /* Kernel killed it, as it should. */
      else
        leaked++;              /* A bad pointer slipped through! */
    }
  printf ("fuzz: survivors %d/%d (%d calls each), bombs killed %d/%d, leaked %d\n",
          survived, nseeds, iters, killed, nseeds, leaked);
  printf ("fuzz: %d confirmed system calls with no panic or hang\n",
          survived * iters);

  /* The kernel must still work.  The base file system can't grow a
     file, so create it with room. */
  create ("fuzz.chk", 16);
  fd = open ("fuzz.chk");
  if (fd > 1 && write (fd, "healthy", 7) == 7)
    {
      seek (fd, 0);
      if (read (fd, buf, 7) == 7 && !memcmp (buf, "healthy", 7))
        printf ("fuzz: kernel healthy after fuzzing\n");
      else
        printf ("fuzz: FAILED readback after fuzzing\n");
    }
  else
    printf ("fuzz: FAILED file ops after fuzzing\n");
  if (fd > 1)
    close (fd);

  return survived == nseeds && leaked == 0 ? 0 : 1;
}

int
main (int argc, char *argv[])
{
  if (argc >= 4 && argv[1][0] == 'P')
    return parent (atoi (argv[2]), atoi (argv[3]),
                   argc > 4 ? atoi (argv[4]) : 2000);
  else if (argc >= 4 && argv[1][0] == 'S')
    return survivor (atoi (argv[2]), atoi (argv[3]));
  else if (argc >= 4 && argv[1][0] == 'B')
    return bomb (atoi (argv[2]), atoi (argv[3]));
  printf ("usage: fuzz P seed nseeds iters | fuzz S|B seed iters\n");
  return 2;
}
