/* Attacker helper for esc-pages / B1.  Resets its own page limit (the
   escape attempt), touches a block of pages to charge them, then spawns
   one more copy one level shallower.  Returns the number of levels built
   below it.  A level that cannot get its pages (the cap bit) is killed by
   the kernel and its parent sees a negative wait result. */
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

#define LIMIT_PAGES 1
#define TOUCH 20
#define PGSIZE 4096

static volatile char block[TOUCH * PGSIZE];

int
main (int argc, char *argv[])
{
  int depth = argc > 1 ? atoi (argv[1]) : 0;
  char cmd[32];
  pid_t pid;
  int r, i;

  setlimit (LIMIT_PAGES, getlimit (LIMIT_PAGES));   /* Escape attempt. */
  for (i = 0; i < TOUCH; i++)                       /* Charge TOUCH pages. */
    block[i * PGSIZE] = (char) i;
  if (depth <= 0)
    return 0;
  snprintf (cmd, sizeof cmd, "esc-pgchild %d", depth - 1);
  pid = exec (cmd);
  if (pid == PID_ERROR)
    return 0;
  r = wait (pid);
  return (r < 0 ? 0 : r) + 1;
}
