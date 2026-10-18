/* B1 regression: a process must not escape its subtree PROCESS limit by
   calling setlimit to mint a fresh full allowance.

   A supervisor caps its subtree at CAP processes, then launches an
   "attacker" that, at every level, resets its own limit (the escape
   attempt) and spawns one more attacker underneath.  With proper
   hierarchical groups the whole subtree still cannot exceed CAP, so the
   chain stops a couple of levels down.  With the old flat-group bug each
   escape handed the caller a brand-new CAP-sized group, so the chain ran
   to its full depth -- an unbounded fork bomb. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define CAP 4

void
test_main (void)
{
  pid_t pid;
  int reached;

  CHECK (setlimit (LIMIT_PROCS, CAP), "process limit set to %d", CAP);
  pid = exec ("esc-child 30");
  CHECK (pid != PID_ERROR, "launched the attacker");
  reached = wait (pid);
  /* Contained: the subtree cap stops the chain well below its 30-deep
     ambition.  The escape bug would let it reach ~30. */
  CHECK (reached >= 0 && reached < CAP, "escape contained below the cap");
}
