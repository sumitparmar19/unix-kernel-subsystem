#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PROCS 0
#define LIMIT_PAGES 1
#define LIMIT_FDS 2
#define LIMIT_CPU 3

/* A CPU spinner with a budget is killed when the budget runs out,
   even though it makes no system calls after setting the limit. */

void
test_main (void)
{
  CHECK (setlimit (LIMIT_CPU, 50), "set CPU budget to 50 ticks");
  msg ("spinning; the kernel should kill us at the budget");
  for (;;)
    continue;
  fail ("should have been killed at the CPU budget");
}
