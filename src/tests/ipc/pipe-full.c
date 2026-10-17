/* Writing more than a pipe holds blocks the writer until a reader
   (a child process) drains it; no data is lost or reordered. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

#define SIZE 8192

static char buf[SIZE];

void
test_main (void)
{
  int fds[2], status, i;
  pid_t pid;

  for (i = 0; i < SIZE; i++)
    buf[i] = i % 251;
  CHECK (pipe (fds), "pipe");
  CHECK ((pid = exec_redirect ("child-drain 8192", fds[0], -1)) != PID_ERROR,
         "start reader with stdin = pipe");
  close (fds[0]);
  CHECK (write (fds[1], buf, SIZE) == SIZE,
         "write 8192 bytes into a 4096-byte pipe");
  close (fds[1]);
  status = wait (pid);
  CHECK (status == 0, "reader got all bytes in order (status %d)", status);
}
