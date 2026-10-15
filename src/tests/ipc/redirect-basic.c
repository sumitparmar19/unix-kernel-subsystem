/* A child's standard output can be a pipe the parent reads. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char buf[64];
  int fds[2], n, total = 0, status;
  pid_t pid;

  CHECK (pipe (fds), "pipe");
  CHECK ((pid = exec_redirect ("child-echo hello from child", -1, fds[1]))
         != PID_ERROR, "start child with stdout = pipe");
  close (fds[1]);
  while ((n = read (fds[0], buf + total, sizeof buf - 1 - total)) > 0)
    total += n;
  buf[total] = '\0';
  close (fds[0]);
  status = wait (pid);
  CHECK (status == 0, "child exited 0");
  CHECK (!strcmp (buf, "hello from child\n"), "read \"hello from child\"");
}
