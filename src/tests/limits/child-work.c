/* A well-behaved neighbour: does a bounded amount of work and exits
   with a known status. */
#include <syscall.h>

int
main (void)
{
  volatile unsigned x = 0;
  int i;
  for (i = 0; i < 2000000; i++)
    x += i;
  return 99;
}
