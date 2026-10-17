/* Copies standard input to standard output in upper case until end
   of file.  Used as a pipeline filter. */

#include <ctype.h>
#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  char buf[64];
  int n, i;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    {
      for (i = 0; i < n; i++)
        buf[i] = toupper ((unsigned char) buf[i]);
      if (write (STDOUT_FILENO, buf, n) != n)
        return 1;
    }
  return n == 0 ? 0 : 1;
}
