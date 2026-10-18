/* B4 regression: the check-then-pin race in the load path.  Several
   children loop reading and writing a multi-page file into multi-page
   buffers while collectively keeping memory full, so a buffer page can be
   evicted between "is it resident?" and "pin it".  The old code then faulted
   in the kernel while holding filesys_lock -- a non-recursive re-acquire,
   i.e. a panic.  The fix pins resident pages atomically, so the file I/O
   never faults under the lock. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define KIDS 8
#define FILESZ (64 * 1024)      /* 16 pages of file data. */

void
test_main (void)
{
  static char seed[FILESZ];
  pid_t pids[KIDS];
  int fd, i, ok = 0;

  for (i = 0; i < FILESZ; i++)
    seed[i] = (char) (i * 5 + 3);
  CHECK (create ("rw.dat", FILESZ), "create the data file");
  CHECK ((fd = open ("rw.dat")) >= 0, "open to seed");
  CHECK (write (fd, seed, FILESZ) == FILESZ, "seed %d bytes", FILESZ);
  close (fd);

  for (i = 0; i < KIDS; i++)
    pids[i] = exec ("grind-child rw.dat");
  for (i = 0; i < KIDS; i++)
    ok += pids[i] != PID_ERROR && wait (pids[i]) == 0;
  CHECK (ok == KIDS, "all %d readers/writers finished without a kernel fault",
         KIDS);
}
