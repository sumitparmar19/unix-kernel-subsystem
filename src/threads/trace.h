#ifndef THREADS_TRACE_H
#define THREADS_TRACE_H

/* Kernel event tracing.  With the `-trace' kernel
   option, events go into a fixed-size ring buffer, timestamped with
   the CPU's time-stamp counter, and are dumped over the serial port
   at power-off.  scripts/trace2perfetto.py turns the dump into Chrome
   trace JSON for ui.perfetto.dev.  Without `-trace' every hook is a
   single branch. */

#include <stdbool.h>
#include <stdint.h>

enum trace_type
  {
    TR_SWITCH,              /* Context switch; arg = next tid. */
    TR_SYSCALL_ENTER,       /* arg = system call number. */
    TR_SYSCALL_EXIT,        /* arg = system call number. */
    TR_LOCK_WAIT,           /* About to block on a lock; arg = holder tid. */
    TR_LOCK_ACQUIRE,        /* Lock obtained after waiting. */
    TR_DONATE,              /* Priority donated; arg = new priority. */
    TR_PAGE_FAULT,          /* arg = faulting address. */
    TR_EVICT,               /* arg = evicted user page. */
    TR_SWAP_IN,             /* arg = user page. */
    TR_SWAP_OUT,            /* arg = user page. */
    TR_PIPE_BLOCK,          /* Pipe reader/writer blocks; arg = 0 read, 1 write. */
    TR_PIPE_WAKE,           /* ... and wakes up. */
    TR_CNT
  };

extern bool trace_enabled;

void trace_init (void);
void trace_thread_name (int tid, const char *name);
void trace_record (enum trace_type, uint32_t arg);
void trace_dump (void);

/* Records an event if tracing is on. */
static inline void
trace_event (enum trace_type type, uint32_t arg)
{
  if (trace_enabled)
    trace_record (type, arg);
}

#endif /* threads/trace.h */
