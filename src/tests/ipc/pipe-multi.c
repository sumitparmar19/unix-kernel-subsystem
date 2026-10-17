/* Three writer processes share one pipe; the reader sees EOF only
   after all of them (and the parent's write end) have closed. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

#define EACH 3000

void
test_main (void)
{
  static const char *cmds[] = { "child-writer a 3000", "child-writer b 3000",
                                "child-writer c 3000" };
  int counts[3] = { 0, 0, 0 };
  pid_t pids[3];
  char buf[128];
  int fds[2], i, n, ok = 0;

  CHECK (pipe (fds), "pipe");
  for (i = 0; i < 3; i++)
    if ((pids[i] = exec_redirect (cmds[i], -1, fds[1])) == PID_ERROR)
      fail ("exec_redirect %s", cmds[i]);
  msg ("started 3 writers");
  close (fds[1]);
  while ((n = read (fds[0], buf, sizeof buf)) > 0)
    for (i = 0; i < n; i++)
      if (buf[i] >= 'a' && buf[i] <= 'c')
        counts[buf[i] - 'a']++;
  close (fds[0]);
  for (i = 0; i < 3; i++)
    ok += wait (pids[i]) == 0;
  CHECK (ok == 3, "all writers exited 0");
  CHECK (counts[0] == EACH && counts[1] == EACH && counts[2] == EACH,
         "read 3000 bytes from each writer");
}
