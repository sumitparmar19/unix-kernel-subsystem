#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define LIMIT_PAGES 1
#define LIMIT_FDS 2
#define LIMIT_CPU 3

/* With a process limit set, a fork bomb is contained: exec starts
   failing once the subtree hits the cap, and the kernel keeps running.
   Children block reading a pipe so they stay alive (occupying slots)
   without burning CPU; closing the pipe lets them all exit. */

#define CAP 8

void
test_main (void)
{
  int fds[2];
  int created = 0;
  pid_t pids[64];

  CHECK (setlimit (LIMIT_PROCS, CAP), "set process limit to %d", CAP);
  CHECK (pipe (fds), "pipe");
  while (created < 60)
    {
      pid_t pid = exec_redirect ("child-block", fds[0], -1);
      if (pid == PID_ERROR)
        break;
      pids[created++] = pid;
    }
  /* The caller is 1 of CAP, so CAP-1 live children fit. */
  CHECK (created == CAP - 1, "exec was capped at %d children", CAP - 1);
  close (fds[1]);               /* writers gone -> children see EOF, exit */
  int i, ok = 0;
  for (i = 0; i < created; i++)
    ok += wait (pids[i]) == 0;
  CHECK (ok == created, "all %d contained children exited cleanly", created);
  close (fds[0]);
  CHECK (create ("fb.chk", 4), "kernel still works after the bomb");
}
