/* Writes argv[2] copies of the character argv[1][0] to standard
   output, in 100-byte writes. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[])
{
  char buf[100];
  int left;

  if (argc != 3)
    return 1;
  memset (buf, argv[1][0], sizeof buf);
  for (left = atoi (argv[2]); left > 0; left -= sizeof buf)
    {
      int n = left < (int) sizeof buf ? left : (int) sizeof buf;
      if (write (STDOUT_FILENO, buf, n) != n)
        return 1;
    }
  return 0;
}
