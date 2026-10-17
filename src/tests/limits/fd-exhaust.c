#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define LIMIT_PAGES 1
#define LIMIT_FDS 2
#define LIMIT_CPU 3

/* With a descriptor limit set, opening files fails at the cap instead
   of exhausting kernel memory; the process is not killed. */

#define CAP 16

void
test_main (void)
{
  int opened = 0, fd;

  CHECK (create ("f.dat", 8), "create a file to open repeatedly");
  CHECK (setlimit (LIMIT_FDS, CAP), "set descriptor limit to %d", CAP);
  while ((fd = open ("f.dat")) != -1)
    {
      opened++;
      if (opened > CAP)
        break;
    }
  /* fds 0,1 (console) are already used, so CAP-2 opens fit. */
  CHECK (opened == CAP - 2, "open was capped at %d descriptors", CAP - 2);
  msg ("process still alive after hitting the fd cap");
}
