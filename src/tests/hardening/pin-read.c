/* B2 regression: reading a small file into a buffer far larger than
   physical RAM must not pin every frame and panic the kernel.  The old
   read pinned the whole requested range up front (here ~768 pages, more
   than the 367-page user pool), so eviction found no victim and hit
   PANIC ("frame: no evictable frame found").  Bounded, chunked pinning
   reads it in pieces and stops at end of file. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define BIG (3 * 1024 * 1024)   /* > the ~1.5 MB user pool. */
#define SMALL 200

static char big[BIG];

void
test_main (void)
{
  static char small[SMALL];
  int fd, i, n;

  for (i = 0; i < SMALL; i++)
    small[i] = (char) (i * 7 + 1);
  CHECK (create ("pr.dat", SMALL), "create the small file");
  CHECK ((fd = open ("pr.dat")) >= 0, "open to fill it");
  CHECK (write (fd, small, SMALL) == SMALL, "wrote %d bytes", SMALL);
  close (fd);

  CHECK ((fd = open ("pr.dat")) >= 0, "reopen to read");
  n = read (fd, big, BIG);      /* huge buffer, tiny file */
  CHECK (n == SMALL, "read returned the file's %d bytes", SMALL);
  close (fd);

  for (i = 0; i < SMALL; i++)
    if (big[i] != (char) (i * 7 + 1))
      fail ("byte %d wrong (got %d)", i, big[i]);
  CHECK (true, "contents intact; the kernel survived the huge-buffer read");
}
