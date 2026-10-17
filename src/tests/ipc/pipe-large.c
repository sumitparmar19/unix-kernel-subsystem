/* Streams 1 MB through a 4 KB pipe to a child, which returns a
   checksum of everything it read as its exit status. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

#include <random.h>

#define CHUNK 4096
#define CHUNKS 256

static unsigned char buf[CHUNK];

void
test_main (void)
{
  unsigned sum = 0;
  int fds[2], status, i, j;
  pid_t pid;

  CHECK (pipe (fds), "pipe");
  CHECK ((pid = exec_redirect ("child-sum", fds[0], -1)) != PID_ERROR,
         "start checksum child");
  close (fds[0]);
  for (i = 0; i < CHUNKS; i++)
    {
      random_bytes (buf, CHUNK);
      for (j = 0; j < CHUNK; j++)
        sum = (sum * 31 + buf[j]) & 0x7fffffff;
      if (write (fds[1], buf, CHUNK) != CHUNK)
        fail ("write of chunk %d failed", i);
    }
  msg ("wrote 1048576 bytes");
  close (fds[1]);
  status = wait (pid);
  CHECK ((unsigned) status == sum, "child's checksum matches");
}
