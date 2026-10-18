/* Reader helper for pin-pipe / B2.  Blocks reading its (empty) stdin pipe
   into a 1 MB buffer until EOF, then exits 0.  The large buffer is the
   point: under the old unbounded pinning, several of these at once locked
   the whole user pool. */
#include <stdio.h>
#include <syscall.h>

#define BUFSZ (1024 * 1024)

static char buf[BUFSZ];

int
main (void)
{
  int n;
  while ((n = read (STDIN_FILENO, buf, BUFSZ)) > 0)
    continue;
  return 0;
}
