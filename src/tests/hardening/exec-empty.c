/* B5 regression (found by the v2.1 fuzzer): exec of an empty or all-spaces
   command line has no program name.  The loader used to pass the resulting
   NULL straight to the file system, panicking `dir_lookup`.  It must now
   fail the load cleanly and return PID_ERROR, leaving the kernel healthy. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  CHECK (exec ("") == PID_ERROR, "exec of \"\" is refused");
  CHECK (exec ("    ") == PID_ERROR, "exec of all-spaces is refused");
  CHECK (exec_redirect ("", -1, -1) == PID_ERROR, "exec_redirect of \"\" is refused");
  CHECK (create ("ee2.chk", 4), "kernel still works afterward");
}
