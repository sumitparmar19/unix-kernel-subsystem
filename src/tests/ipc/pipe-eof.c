/* A reader sees end of file (read returns 0) once the pipe is empty
   and its write end is closed. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int fds[2];
  char buf[8];

  CHECK (pipe (fds), "pipe");
  CHECK (write (fds[1], "abc", 3) == 3, "write 3 bytes");
  close (fds[1]);
  CHECK (read (fds[0], buf, sizeof buf) == 3, "read the 3 buffered bytes");
  CHECK (read (fds[0], buf, sizeof buf) == 0, "then read returns 0 (EOF)");
  close (fds[0]);
}
