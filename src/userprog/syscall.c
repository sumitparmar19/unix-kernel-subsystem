#include "userprog/syscall.h"
#include <stdio.h>
#include <stdint.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/kstat.h"
#include "threads/trace.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/ofile.h"
#include "userprog/pipe.h"
#include "userprog/process.h"
#include "userprog/umem.h"
#include "userprog/rlimit.h"
#include "devices/timer.h"

/* Serialises every call into the (non-thread-safe) file system.
   Lock-order rule (ADR-002): never block on another process or
   touch unvalidated user memory while holding it. */
struct lock filesys_lock;

/* Longest file name accepted from user programs (bytes, without the
   NUL).  Longer names fail the call (like ENAMETOOLONG); the Pintos
   file system itself allows 14. */
#define PATH_MAX_LEN 127

/* Default descriptor ceiling when no tighter limit is set (like
   RLIMIT_NOFILE).  The per-process cap from setlimit() overrides it. */
#define FD_LIMIT 1024

/* The current process's descriptor ceiling. */
static int
fd_cap (void)
{
  int lim = rlimit_get (thread_current (), LIMIT_FDS);
  return lim == RLIMIT_NONE || lim > FD_LIMIT ? FD_LIMIT : lim;
}

/* A system call handler: takes the raw 32-bit arguments and returns
   the value for the caller's eax. */
typedef uint32_t syscall_func (const uint32_t *args);

/* Ends the current process with exit status -1.  Used whenever a
   system call gets an invalid pointer or argument. */
static void NO_RETURN
kill_process (void)
{
  thread_current ()->exit_status = -1;
  thread_exit ();
}

/* Copies the user path at UPATH into KBUF (PATH_MAX_LEN + 1 bytes).
   Kills the process if UPATH is invalid memory; returns false if the
   name is too long. */
static bool
get_path (char *kbuf, const char *upath)
{
  int len = copy_in_string (kbuf, upath, PATH_MAX_LEN + 1);
  if (len == UMEM_FAULT)
    kill_process ();
  return len != UMEM_TOO_LONG;
}

/* Returns the current process's descriptor table. */
static struct fdtable *
fds (void)
{
  return &thread_current ()->fds;
}

/* Returns the file behind descriptor FD, or NULL if FD is not an
   open file (closed, console or pipe). */
static struct file *
fd_file (int fd)
{
  struct ofile *of = fd_lookup (fds (), fd);
  return of != NULL && of->type == OF_FILE ? of->file : NULL;
}

static uint32_t
sys_halt (const uint32_t *args UNUSED)
{
  shutdown_power_off ();
}

static uint32_t
sys_exit (const uint32_t *args)
{
  thread_current ()->exit_status = (int) args[0];
  thread_exit ();
}

static uint32_t
sys_exec (const uint32_t *args)
{
  char *cmd_line = palloc_get_page (0);
  int len;
  tid_t tid;

  if (cmd_line == NULL)
    return (uint32_t) -1;
  len = copy_in_string (cmd_line, (const char *) args[0], PGSIZE);
  if (len == UMEM_FAULT)
    {
      palloc_free_page (cmd_line);
      kill_process ();
    }
  tid = len == UMEM_TOO_LONG ? TID_ERROR : process_execute (cmd_line);
  palloc_free_page (cmd_line);
  return (uint32_t) tid;
}

static uint32_t
sys_wait (const uint32_t *args)
{
  return (uint32_t) process_wait ((tid_t) args[0]);
}

static uint32_t
sys_create (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  bool ok;

  if (!get_path (name, (const char *) args[0]))
    return false;
  lock_acquire (&filesys_lock);
  ok = filesys_create (name, (off_t) args[1]);
  lock_release (&filesys_lock);
  return ok;
}

static uint32_t
sys_remove (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  bool ok;

  if (!get_path (name, (const char *) args[0]))
    return false;
  lock_acquire (&filesys_lock);
  ok = filesys_remove (name);
  lock_release (&filesys_lock);
  return ok;
}

static uint32_t
sys_open (const uint32_t *args)
{
  char name[PATH_MAX_LEN + 1];
  struct ofile *of;
  struct file *file;
  int fd;

  if (!get_path (name, (const char *) args[0]))
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  file = filesys_open (name);
  lock_release (&filesys_lock);
  if (file == NULL)
    return (uint32_t) -1;

  of = ofile_from_file (file);
  if (of == NULL)
    {
      lock_acquire (&filesys_lock);
      file_close (file);
      lock_release (&filesys_lock);
      return (uint32_t) -1;
    }
  fd = fd_install (fds (), of, fd_cap ());
  if (fd < 0)
    ofile_unref (of);
  return (uint32_t) fd;
}

static uint32_t
sys_filesize (const uint32_t *args)
{
  struct file *file = fd_file ((int) args[0]);
  off_t size;

  if (file == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  size = file_length (file);
  lock_release (&filesys_lock);
  return size;
}

static uint32_t
sys_read (const uint32_t *args)
{
  struct ofile *of = fd_lookup (fds (), (int) args[0]);
  void *buffer = (void *) args[1];
  unsigned size = args[2];

  /* The kernel writes into BUFFER: every page must be writable. */
  if (!validate_range (buffer, size, true))
    kill_process ();
  return of != NULL ? (uint32_t) ofile_read (of, buffer, size) : (uint32_t) -1;
}

static uint32_t
sys_write (const uint32_t *args)
{
  struct ofile *of = fd_lookup (fds (), (int) args[0]);
  const void *buffer = (const void *) args[1];
  unsigned size = args[2];

  if (!validate_range (buffer, size, false))
    kill_process ();
  return of != NULL ? (uint32_t) ofile_write (of, buffer, size) : (uint32_t) -1;
}

static uint32_t
sys_seek (const uint32_t *args)
{
  struct file *file = fd_file ((int) args[0]);

  if (file != NULL)
    {
      lock_acquire (&filesys_lock);
      file_seek (file, (off_t) args[1]);
      lock_release (&filesys_lock);
    }
  return 0;
}

static uint32_t
sys_tell (const uint32_t *args)
{
  struct file *file = fd_file ((int) args[0]);
  off_t pos;

  if (file == NULL)
    return (uint32_t) -1;
  lock_acquire (&filesys_lock);
  pos = file_tell (file);
  lock_release (&filesys_lock);
  return pos;
}

/* Closing a descriptor that refers to the console is a no-op, so
   the console stays usable (the stock Pintos contract that
   close-stdin/close-stdout test).  A redirected fd 0 or 1 (a pipe or
   file) closes normally.  Documented in docs/SYSCALLS.md. */
static uint32_t
sys_close (const uint32_t *args)
{
  int fd = (int) args[0];
  struct ofile *of = fd_lookup (fds (), fd);

  if (of != NULL && !ofile_is_console (of))
    fd_close (fds (), fd);
  return 0;
}

/* bool pipe (int fds[2]) */
static uint32_t
sys_pipe (const uint32_t *args)
{
  int *ufds = (int *) args[0];
  struct ofile *rd, *wr;
  struct pipe *p;
  int kfds[2];

  /* Validate the output array first, so nothing is created and then
     leaked when the process is killed. */
  if (!validate_range (ufds, sizeof kfds, true))
    kill_process ();

  p = pipe_create ();
  if (p == NULL)
    return false;
  rd = ofile_from_pipe (p, false);
  wr = ofile_from_pipe (p, true);
  if (rd == NULL || wr == NULL)
    {
      /* Close whichever ends have no object yet, then the objects. */
      if (rd == NULL)
        pipe_close (p, false);
      if (wr == NULL)
        pipe_close (p, true);
      ofile_unref (rd);
      ofile_unref (wr);
      return false;
    }

  kfds[0] = fd_install (fds (), rd, fd_cap ());
  kfds[1] = kfds[0] < 0 ? -1 : fd_install (fds (), wr, fd_cap ());
  if (kfds[1] < 0)
    {
      if (kfds[0] >= 0)
        fd_close (fds (), kfds[0]);
      else
        ofile_unref (rd);
      ofile_unref (wr);
      return false;
    }
  if (!copy_out (ufds, kfds, sizeof kfds))
    kill_process ();            /* Validated above; can't happen. */
  return true;
}

/* Returns a new reference to the object behind FD for a child's
   standard stream, the console object DEFAULT if FD is -1, or NULL
   if FD is not open. */
static struct ofile *
stream_for_child (int fd, struct ofile *default_of)
{
  struct ofile *of = fd == -1 ? default_of : fd_lookup (fds (), fd);
  return of != NULL ? ofile_ref (of) : NULL;
}

/* pid_t exec_redirect (const char *cmd_line, int stdin_fd, int stdout_fd) */
static uint32_t
sys_exec_redirect (const uint32_t *args)
{
  struct ofile *in, *out;
  char *cmd_line;
  int len;

  cmd_line = palloc_get_page (0);
  if (cmd_line == NULL)
    return (uint32_t) -1;
  len = copy_in_string (cmd_line, (const char *) args[0], PGSIZE);
  if (len == UMEM_FAULT)
    {
      palloc_free_page (cmd_line);
      kill_process ();
    }

  in = stream_for_child ((int) args[1], ofile_console_in ());
  out = stream_for_child ((int) args[2], ofile_console_out ());
  if (len == UMEM_TOO_LONG || in == NULL || out == NULL)
    {
      ofile_unref (in);
      ofile_unref (out);
      palloc_free_page (cmd_line);
      return (uint32_t) -1;
    }

  /* process_execute_redirect() takes over both references. */
  len = process_execute_redirect (cmd_line, in, out);
  palloc_free_page (cmd_line);
  return (uint32_t) len;
}

/* int dup2 (int oldfd, int newfd) */
static uint32_t
sys_dup2 (const uint32_t *args)
{
  return (uint32_t) fd_dup2 (fds (), (int) args[0], (int) args[1], fd_cap ());
}

/* bool setlimit (int resource, int value) */
static uint32_t
sys_setlimit (const uint32_t *args)
{
  int res = (int) args[0];
  if (res < 0 || res >= LIMIT_CNT)
    return false;
  return rlimit_set (thread_current (), res, (int) args[1]);
}

/* int getlimit (int resource) */
static uint32_t
sys_getlimit (const uint32_t *args)
{
  int res = (int) args[0];
  if (res < 0 || res >= LIMIT_CNT)
    return (uint32_t) RLIMIT_NONE;
  return (uint32_t) rlimit_get (thread_current (), res);
}

/* int uptime (void) -- timer ticks since boot. */
static uint32_t
sys_uptime (const uint32_t *args UNUSED)
{
  return (uint32_t) timer_ticks ();
}

/* System call table: handler and number of 32-bit arguments. */
static const struct
  {
    syscall_func *func;
    int argc;
  }
syscalls[] =
  {
    [SYS_HALT]     = { sys_halt,     0 },
    [SYS_EXIT]     = { sys_exit,     1 },
    [SYS_EXEC]     = { sys_exec,     1 },
    [SYS_WAIT]     = { sys_wait,     1 },
    [SYS_CREATE]   = { sys_create,   2 },
    [SYS_REMOVE]   = { sys_remove,   1 },
    [SYS_OPEN]     = { sys_open,     1 },
    [SYS_FILESIZE] = { sys_filesize, 1 },
    [SYS_READ]     = { sys_read,     3 },
    [SYS_WRITE]    = { sys_write,    3 },
    [SYS_SEEK]     = { sys_seek,     2 },
    [SYS_TELL]     = { sys_tell,     1 },
    [SYS_CLOSE]    = { sys_close,    1 },
    [SYS_PIPE]          = { sys_pipe,          1 },
    [SYS_EXEC_REDIRECT] = { sys_exec_redirect, 3 },
    [SYS_DUP2]          = { sys_dup2,          2 },
    [SYS_SETLIMIT]      = { sys_setlimit,      2 },
    [SYS_GETLIMIT]      = { sys_getlimit,      1 },
    [SYS_UPTIME]        = { sys_uptime,        0 },
  };
#define SYSCALL_CNT (sizeof syscalls / sizeof *syscalls)

/* Calls made, per system call number. */
static uint32_t syscall_counts[SYSCALL_CNT];

/* Entry point for `int $0x30'.  The user stack holds the system call
   number followed by its arguments; every one of those words is
   copied in through umem (ADR-001) before use. */
static void
syscall_handler (struct intr_frame *f)
{
  uintptr_t usp = (uintptr_t) f->esp;
  uint32_t number;
  uint32_t args[3];
  int i;

  if (!copy_in (&number, (const void *) usp, sizeof number)
      || number >= SYSCALL_CNT || syscalls[number].func == NULL)
    kill_process ();
  for (i = 0; i < syscalls[number].argc; i++)
    if (!copy_in (&args[i], (const void *) (usp + 4 * (i + 1)),
                  sizeof args[i]))
      kill_process ();

  kstat_inc (KS_SYSCALLS);
  syscall_counts[number]++;
  trace_event (TR_SYSCALL_ENTER, number);
  f->eax = syscalls[number].func (args);
  trace_event (TR_SYSCALL_EXIT, number);
}

/* Prints how often each system call was made, as one
   `KSTAT-SYSCALLS number=count ...' line (calls made at least once). */
void
syscall_print_stats (void)
{
  size_t i;

  printf ("KSTAT-SYSCALLS");
  for (i = 0; i < SYSCALL_CNT; i++)
    if (syscall_counts[i] != 0)
      printf (" %u=%u", (unsigned) i, (unsigned) syscall_counts[i]);
  printf ("\n");
}

/* Registers the system call interrupt handler. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}
