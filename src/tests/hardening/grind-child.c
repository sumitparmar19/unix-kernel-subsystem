/* Worker helper for rw-pressure / B4.  Repeatedly reads a multi-page file
   into a multi-page buffer and writes it back, touching a large pressure
   array in between so the buffer's pages are evicted and must be faulted
   back in on the next access -- driving the load path's pin hard while
   memory is full. */
#include <syscall.h>

#define FILESZ (64 * 1024)              /* 16 pages of file data. */
#define BUFSZ FILESZ
#define PRESSURE (160 * 4096)           /* Force eviction of the buffer. */
#define ITERS 60

static char buf[BUFSZ];
static volatile char pressure[PRESSURE];

int
main (int argc, char *argv[])
{
  const char *name = argc > 1 ? argv[1] : "rw.dat";
  int fd, i, t;

  if ((fd = open (name)) < 0)
    return 1;
  for (t = 0; t < ITERS; t++)
    {
      seek (fd, 0);
      read (fd, buf, BUFSZ);            /* 16 pages pinned+faulted per read */
      for (i = 0; i < PRESSURE; i += 4096)
        pressure[i] = (char) (t + i);  /* evict the buffer pages */
      seek (fd, 0);
      write (fd, buf, BUFSZ);          /* reload+pin them under pressure */
      for (i = 0; i < PRESSURE; i += 4096)
        pressure[i] = (char) (t + i + 1);
    }
  close (fd);
  return 0;
}
