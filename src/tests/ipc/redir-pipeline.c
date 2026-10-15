/* producer | filter | consumer: three processes connected by two
   pipes.  The consumer writes the result to the console. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int p1[2], p2[2], s1, s2, s3;
  pid_t producer, filter, consumer;

  CHECK (pipe (p1) && pipe (p2), "two pipes");
  producer = exec_redirect ("child-echo pipes connect processes", -1, p1[1]);
  filter = exec_redirect ("child-upper", p1[0], p2[1]);
  consumer = exec_redirect ("child-cat", p2[0], -1);
  if (producer == PID_ERROR || filter == PID_ERROR || consumer == PID_ERROR)
    fail ("exec_redirect failed");
  /* Without these closes the filter and consumer never see EOF. */
  close (p1[0]);
  close (p1[1]);
  close (p2[0]);
  close (p2[1]);
  s3 = wait (consumer);
  s2 = wait (filter);
  s1 = wait (producer);
  CHECK (s1 == 0 && s2 == 0 && s3 == 0, "all three processes exited 0");
}
