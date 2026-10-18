/* B2 regression, write side: writing from a buffer far larger than RAM to
   a small file must not pin every frame and panic.  The write stops at the
   file's fixed size; bounded pinning keeps it to a few frames at a time. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define BIG (3 * 1024 * 1024)
#define SMALL 200

static char big[BIG];           /* BSS: all zero. */

void
test_main (void)
{
  static char back[SMALL];
  int fd, i, n;

  CHECK (create ("pw.dat", SMALL), "create the small file");
  CHECK ((fd = open ("pw.dat")) >= 0, "open to write");
  n = write (fd, big, BIG);     /* huge buffer, file holds only SMALL */
  CHECK (n == SMALL, "write capped at the file's %d bytes", SMALL);
  close (fd);

  CHECK ((fd = open ("pw.dat")) >= 0, "reopen to verify");
  CHECK (read (fd, back, SMALL) == SMALL, "read the %d bytes back", SMALL);
  close (fd);
  for (i = 0; i < SMALL; i++)
    if (back[i] != 0)
      fail ("byte %d not zero (got %d)", i, back[i]);
  CHECK (true, "contents intact; the kernel survived the huge-buffer write");
}
