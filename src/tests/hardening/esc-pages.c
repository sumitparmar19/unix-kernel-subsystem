/* B1 regression, page variant: a process must not escape its subtree PAGE
   limit by calling setlimit.  A supervisor caps the subtree's pages; an
   attacker chain, at each level, resets its page limit (escape attempt),
   touches a block of pages, and spawns the next level.  Proper hierarchy
   bounds the subtree's total pages, so only a few levels fit; the old bug
   gave each level a fresh full page budget, so the chain ran to depth. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PAGES 1
#define CAP_PAGES 80
#define TOUCH 20                /* Pages each level writes. */

void
test_main (void)
{
  pid_t pid;
  int reached;

  CHECK (setlimit (LIMIT_PAGES, CAP_PAGES), "page limit set to %d", CAP_PAGES);
  pid = exec ("esc-pgchild 30");
  CHECK (pid != PID_ERROR, "launched the attacker");
  reached = wait (pid);
  /* With CAP_PAGES pages shared across the subtree, only a handful of
     TOUCH-page levels fit.  The escape bug would let the chain reach ~30. */
  CHECK (reached >= 0 && reached < CAP_PAGES / TOUCH + 1,
         "escape contained below the cap");
}
