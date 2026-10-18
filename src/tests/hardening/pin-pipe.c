/* B2 regression, pipe side: many processes blocking in a pipe read with
   large buffers must not each pin their whole buffer -- that would lock all
   of memory and panic.  Eight children each block reading into a 1 MB
   buffer (256 pages); the old code pinned all of it, so two children alone
   exceeded the 367-page pool.  Bounded pinning holds only a chunk per
   blocked reader, so all eight coexist and exit cleanly at EOF. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define KIDS 8

void
test_main (void)
{
  int fds[2];
  pid_t pids[KIDS];
  int i, ok = 0;

  CHECK (pipe (fds), "pipe");
  for (i = 0; i < KIDS; i++)
    {
      pids[i] = exec_redirect ("pin-pipe-rd", fds[0], -1);
      if (pids[i] == PID_ERROR)
        fail ("could not launch reader %d", i);
    }
  /* All KIDS are now (or soon) blocked in read, each holding only a bounded
     pin.  Closing the write end gives them EOF so they return 0. */
  close (fds[1]);
  for (i = 0; i < KIDS; i++)
    ok += wait (pids[i]) == 0;
  close (fds[0]);
  CHECK (ok == KIDS, "all %d blocked huge-buffer readers exited cleanly", KIDS);
  CHECK (create ("pp.chk", 4), "kernel survived the blocked readers");
}
