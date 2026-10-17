/* Writing to a pipe whose read end is closed fails with -1. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int fds[2];

  CHECK (pipe (fds), "pipe");
  close (fds[0]);
  CHECK (write (fds[1], "x", 1) == -1, "write with no reader returns -1");
  close (fds[1]);
}
