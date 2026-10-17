#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define LIMIT_PAGES 1
#define LIMIT_FDS 2
#define LIMIT_CPU 3

/* getlimit reports the current caps; setlimit can only lower them. */

void
test_main (void)
{
  CHECK (getlimit (LIMIT_FDS) == -1, "fd limit starts unlimited");
  CHECK (setlimit (LIMIT_FDS, 32), "lower fd limit to 32");
  CHECK (getlimit (LIMIT_FDS) == 32, "getlimit reports 32");
  CHECK (!setlimit (LIMIT_FDS, 64), "raising the fd limit is refused");
  CHECK (getlimit (LIMIT_FDS) == 32, "fd limit is still 32");
  CHECK (setlimit (LIMIT_FDS, 8), "lowering it again to 8 works");
  CHECK (getlimit (LIMIT_FDS) == 8, "getlimit reports 8");
}
