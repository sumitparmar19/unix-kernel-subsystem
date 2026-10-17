#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define LIMIT_PAGES 1

/* With a page limit set, a process that grows its memory (here, deep
   recursion that touches a fresh stack page per frame) is killed when
   it exceeds the cap, instead of exhausting the machine. */

static void
grow (int n)
{
  volatile char pad[4096];      /* One fresh stack page per call. */
  pad[0] = n;
  pad[4095] = n;
  if (n > 0)
    grow (n - 1);
}

void
test_main (void)
{
  CHECK (setlimit (LIMIT_PAGES, 64), "set page limit to 64");
  msg ("growing memory past the page limit");
  grow (500);
  fail ("should have been killed at the page limit");
}
