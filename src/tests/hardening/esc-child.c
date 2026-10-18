/* Attacker helper for esc-procs / B1.  Resets its own process limit (the
   escape attempt), then spawns one more copy of itself one level shallower
   and waits for it.  Returns how many levels it managed to build below
   itself, so the supervisor can see whether the subtree cap contained the
   chain. */
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

#define LIMIT_PROCS 0

int
main (int argc, char *argv[])
{
  int depth = argc > 1 ? atoi (argv[1]) : 0;
  char cmd[32];
  pid_t pid;
  int r;

  setlimit (LIMIT_PROCS, getlimit (LIMIT_PROCS));   /* Escape attempt. */
  if (depth <= 0)
    return 0;
  snprintf (cmd, sizeof cmd, "esc-child %d", depth - 1);
  pid = exec (cmd);
  if (pid == PID_ERROR)
    return 0;                   /* Cap reached: cannot spawn any deeper. */
  r = wait (pid);
  return (r < 0 ? 0 : r) + 1;
}
