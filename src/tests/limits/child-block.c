/* Reads standard input until EOF, then exits 0.  Used to occupy a
   process slot without burning CPU. */
#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  char c;
  while (read (STDIN_FILENO, &c, 1) == 1)
    continue;
  return 0;
}
