#ifndef THREADS_KSTAT_H
#define THREADS_KSTAT_H

/* Kernel statistics counters.  Printed at power-off
   as one machine-readable `KSTAT name=value ...' line, after the
   point the test checker reads, so tests are unaffected. */

#include <stdint.h>

enum kstat_counter
  {
    KS_SYSCALLS,            /* System calls dispatched. */
    KS_CTX_SWITCHES,        /* Thread switches. */
    KS_LOCK_WAITS,          /* lock_acquire() calls that had to block. */
    KS_PAGE_FAULTS,         /* Page faults (any cause). */
    KS_EVICTIONS,           /* Frames evicted (VM). */
    KS_SWAP_INS,            /* Pages read back from swap (VM). */
    KS_SWAP_OUTS,           /* Pages written to swap (VM). */
    KS_DIRTY_WRITEBACKS,    /* Dirty mmap pages written to files (VM). */
    KS_PIPE_BYTES,          /* Bytes written into pipes. */
    KS_PIPE_BLOCKS,         /* Times a pipe reader or writer blocked. */
    KS_LIMIT_DENIALS,       /* Requests refused by a resource limit. */
    KS_LIMIT_KILLS,         /* Processes ended by a resource limit. */
    KS_CNT
  };

extern uint32_t kstat[KS_CNT];

/* Adds one to counter C.  Each counter is updated from only one kind
   of context, so a plain increment is enough on this uniprocessor. */
static inline void
kstat_inc (enum kstat_counter c)
{
  kstat[c]++;
}

void kstat_print (void);

#endif /* threads/kstat.h */
