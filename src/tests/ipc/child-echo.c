/* Writes its arguments, separated by spaces, plus a newline to
   standard output.  Used as a pipeline producer. */

#include <string.h>
#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[])
{
  int i;
  for (i = 1; i < argc; i++)
    {
      if (i > 1)
        write (STDOUT_FILENO, " ", 1);
      write (STDOUT_FILENO, argv[i], strlen (argv[i]));
    }
  write (STDOUT_FILENO, "\n", 1);
  return 0;
}
