#include "threads/kstat.h"
#include <stdio.h>

uint32_t kstat[KS_CNT];

static const char *const names[KS_CNT] =
  {
    "syscalls", "ctx_switches", "lock_waits", "page_faults", "evictions",
    "swap_ins", "swap_outs", "dirty_writebacks", "pipe_bytes",
    "pipe_blocks", "limit_denials", "limit_kills",
  };

/* Prints every counter on one line. */
void
kstat_print (void)
{
  int i;

  printf ("KSTAT");
  for (i = 0; i < KS_CNT; i++)
    printf (" %s=%u", names[i], (unsigned) kstat[i]);
  printf ("\n");
}
