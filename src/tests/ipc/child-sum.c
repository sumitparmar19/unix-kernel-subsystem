/* Reads standard input until end of file and exits with a 31-bit
   rolling checksum of everything it read. */

#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  unsigned char buf[512];
  unsigned sum = 0;
  int n, i;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    for (i = 0; i < n; i++)
      sum = (sum * 31 + buf[i]) & 0x7fffffff;
  return n == 0 ? (int) sum : -1;
}
