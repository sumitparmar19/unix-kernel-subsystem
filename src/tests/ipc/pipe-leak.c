/* Creates and destroys many pipes and redirected children.  Each
   pipe uses a page of kernel memory, so if pipes or descriptors
   leaked, this would exhaust the kernel's memory. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char buf[32];
  int fds[2], i;

  for (i = 0; i < 500; i++)
    {
      if (!pipe (fds))
        fail ("pipe %d failed", i);
      close (fds[0]);
      close (fds[1]);
    }
  msg ("created and closed 500 pipes");

  for (i = 0; i < 50; i++)
    {
      pid_t pid;
      if (!pipe (fds))
        fail ("pipe %d failed", i);
      pid = exec_redirect ("child-echo x", -1, fds[1]);
      if (pid == PID_ERROR)
        fail ("exec_redirect %d failed", i);
      close (fds[1]);
      while (read (fds[0], buf, sizeof buf) > 0)
        continue;
      close (fds[0]);
      if (wait (pid) != 0)
        fail ("child %d failed", i);
    }
  msg ("ran 50 redirected children");
}
