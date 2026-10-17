#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define LIMIT_PAGES 1
#define LIMIT_FDS 2
#define LIMIT_CPU 3

/* The isolation payoff: a well-behaved neighbour started next to a
   fork bomb and a CPU spinner still finishes, and within a time bound.
   The abusers are each self-limited (they set their own caps), so they
   cannot starve the neighbour. */

#define DEADLINE 2000           /* timer ticks (~20 s) */

void
test_main (void)
{
  int start = uptime ();
  pid_t bomb, spin, good;
  int gstatus;

  bomb = exec ("abuser-fork");
  spin = exec ("abuser-spin");
  CHECK (bomb != PID_ERROR && spin != PID_ERROR, "started the two abusers");

  good = exec ("child-work");
  CHECK (good != PID_ERROR, "started the well-behaved neighbour");
  gstatus = wait (good);
  int elapsed = uptime () - start;

  CHECK (gstatus == 99, "neighbour computed the right answer");
  CHECK (elapsed < DEADLINE, "neighbour finished within the deadline");
  (void) elapsed;
  /* Reap the abusers so the test ends cleanly. */
  wait (bomb);
  wait (spin);
}
