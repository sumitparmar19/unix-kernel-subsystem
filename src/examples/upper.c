/* upper: copies standard input to standard output in upper case. */

#include <ctype.h>
#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  char buf[128];
  int n, i;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    {
      for (i = 0; i < n; i++)
        buf[i] = toupper ((unsigned char) buf[i]);
      write (STDOUT_FILENO, buf, n);
    }
  return 0;
}
