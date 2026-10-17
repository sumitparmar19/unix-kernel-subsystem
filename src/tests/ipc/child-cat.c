/* Copies standard input to standard output until end of file. */

#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  char buf[64];
  int n;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    if (write (STDOUT_FILENO, buf, n) != n)
      return 1;
  return n == 0 ? 0 : 1;
}
