/* A pipe carries bytes from its write end to its read end. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int fds[2];
  char buf[16];

  CHECK (pipe (fds), "pipe");
  CHECK (fds[0] > 1 && fds[1] > 1 && fds[0] != fds[1],
         "two new descriptors");
  CHECK (write (fds[1], "hello", 6) == 6, "write 6 bytes");
  CHECK (read (fds[0], buf, sizeof buf) == 6, "read 6 bytes");
  CHECK (!strcmp (buf, "hello"), "data matches");
  close (fds[0]);
  close (fds[1]);
}
