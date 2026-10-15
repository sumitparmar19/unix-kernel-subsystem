/* Passing a kernel address as the descriptor array kills the
   caller with -1; the kernel keeps running. */

#include <syscall.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  pipe ((int *) 0xc0000000);
  fail ("should have exited with -1");
}
