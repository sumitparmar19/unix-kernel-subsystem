/* dup2 shares one open file (and its offset) between descriptors,
   and can redirect standard output to a file and back. */

#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char buf[16];
  int fd;

  CHECK (create ("dup.txt", 6), "create \"dup.txt\" (6 bytes)");
  CHECK ((fd = open ("dup.txt")) > 1, "open \"dup.txt\"");
  CHECK (dup2 (fd, 7) == 7, "dup2 (fd, 7)");
  CHECK (write (7, "abc", 3) == 3, "write through fd 7");
  CHECK (tell (fd) == 3, "the original fd shares the offset");
  CHECK (dup2 (fd, fd) == fd, "dup2 of a descriptor onto itself");
  CHECK (dup2 (123, 5) == -1, "dup2 from a closed descriptor fails");
  CHECK (dup2 (fd, -1) == -1, "dup2 to a negative descriptor fails");

  /* Save the console, point stdout at the file, write, restore. */
  CHECK (dup2 (STDOUT_FILENO, 9) == 9, "save stdout as fd 9");
  if (dup2 (fd, STDOUT_FILENO) != STDOUT_FILENO
      || write (STDOUT_FILENO, "def", 3) != 3
      || dup2 (9, STDOUT_FILENO) != STDOUT_FILENO)
    fail ("redirecting stdout to the file");
  close (9);
  msg ("stdout restored");

  seek (fd, 0);
  memset (buf, 0, sizeof buf);
  CHECK (read (fd, buf, sizeof buf) == 6, "file holds 6 bytes");
  CHECK (!strcmp (buf, "abcdef"), "file reads \"abcdef\"");
  close (7);
  close (fd);
}
