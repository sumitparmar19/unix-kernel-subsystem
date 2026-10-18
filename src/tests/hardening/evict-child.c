/* Child helper for exit-evict / B3.  Touches a block of pages (more than
   stay resident, so some get evicted), then exits immediately -- so its
   teardown races against the parent's and siblings' paging. */
#include <syscall.h>

#define PAGES 100
#define PGSIZE 4096

static volatile char block[PAGES * PGSIZE];

int
main (void)
{
  int i;
  for (i = 0; i < PAGES; i++)
    block[i * PGSIZE] = (char) i;
  return 0;
}
