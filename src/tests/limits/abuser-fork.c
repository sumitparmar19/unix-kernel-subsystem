/* A fork bomb that limits ITS OWN subtree, so it cannot starve the
   rest of the system.  Spawns blocked children up to its cap. */
#include <syscall.h>
#define LIMIT_PROCS 0
int
main (void)
{
  int fds[2];
  setlimit (LIMIT_PROCS, 6);
  if (!pipe (fds))
    return 1;
  while (exec_redirect ("child-block", fds[0], -1) != PID_ERROR)
    continue;
  /* Hold the children a while, then release them. */
  volatile unsigned x = 0; int i;
  for (i = 0; i < 1000000; i++) x += i;
  close (fds[1]);
  return 0;
}
