/* Reads standard input until end of file and checks that it got
   exactly argv[1] bytes following the pattern i % 251.  Exits 0 on
   success, 1 otherwise. */

#include <stdlib.h>
#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[])
{
  unsigned char buf[100];
  int expected = argc > 1 ? atoi (argv[1]) : 0;
  int count = 0, n, i, ok = 1;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    for (i = 0; i < n; i++, count++)
      if (buf[i] != count % 251)
        ok = 0;
  return ok && n == 0 && count == expected ? 0 : 1;
}
