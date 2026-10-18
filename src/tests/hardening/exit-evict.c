/* B3 regression: a process exiting while another is evicting one of its
   pages must not free a frame the evictor still holds (use-after-free /
   double-free).  Each round spawns several children that each touch far
   more pages than fit in RAM and then exit at staggered times, while the
   parent also pages heavily -- so evictions and process teardowns overlap,
   exactly the window the bug lived in.  Many rounds make the race likely.
   The fix (owners wait out an in-progress eviction before freeing the
   frame) keeps every round clean. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define ROUNDS 30
#define KIDS 5
#define PGSIZE 4096
#define MINE 150                /* Pages the parent churns each round. */

static volatile char mine[MINE * PGSIZE];

void
test_main (void)
{
  int r, i, k;

  for (r = 0; r < ROUNDS; r++)
    {
      pid_t pids[KIDS];
      for (k = 0; k < KIDS; k++)
        pids[k] = exec ("evict-child");
      /* Churn our own pages so the evictor is busy while the children run
         and tear down. */
      for (i = 0; i < MINE; i++)
        mine[i * PGSIZE] = (char) (r + i);
      for (k = 0; k < KIDS; k++)
        if (pids[k] != PID_ERROR)
          wait (pids[k]);
    }
  CHECK (create ("ee.chk", 4), "survived %d rounds of exit-during-eviction",
         ROUNDS);
}
