/* wc: counts lines, words and bytes on standard input. */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <syscall.h>

int
main (void)
{
  char buf[128];
  int lines = 0, words = 0, bytes = 0, n, i;
  bool in_word = false;

  while ((n = read (STDIN_FILENO, buf, sizeof buf)) > 0)
    for (i = 0; i < n; i++)
      {
        bytes++;
        if (buf[i] == '\n')
          lines++;
        if (isspace ((unsigned char) buf[i]))
          in_word = false;
        else if (!in_word)
          {
            in_word = true;
            words++;
          }
      }
  printf ("%d %d %d\n", lines, words, bytes);
  return 0;
}
