/* A CPU hog that caps its own CPU budget, so the scheduler reclaims
   it and the neighbour still runs. */
#include <syscall.h>
#define LIMIT_CPU 3
int
main (void)
{
  setlimit (LIMIT_CPU, 300);
  for (;;)
    continue;
  return 0;
}
