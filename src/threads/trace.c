#include "threads/trace.h"
#include <stdio.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Set by the `-trace' kernel option. */
bool trace_enabled;

struct trace_rec
  {
    uint64_t tsc;           /* CPU time-stamp counter. */
    int32_t tid;            /* Running thread. */
    uint32_t arg;
    uint16_t type;
    uint16_t pad;
  };

#define TRACE_PAGES 32
#define TRACE_CAP (TRACE_PAGES * PGSIZE / sizeof (struct trace_rec))

static struct trace_rec *ring;      /* TRACE_CAP records, or NULL. */
static uint32_t next;               /* Records ever written. */

/* Thread names, indexed by tid modulo NAME_SLOTS. */
#define NAME_SLOTS 256
static char names[NAME_SLOTS][16];

/* Clock calibration: TSC and timer ticks at start. */
static uint64_t tsc0;
static int64_t tick0;

static inline uint64_t
rdtsc (void)
{
  uint32_t lo, hi;
  asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
  return ((uint64_t) hi << 32) | lo;
}

/* Allocates the ring buffer if tracing was requested.  Call after
   palloc_init() and before threads other than main start. */
void
trace_init (void)
{
  if (!trace_enabled)
    return;
  ring = palloc_get_multiple (PAL_ZERO, TRACE_PAGES);
  if (ring == NULL)
    {
      printf ("trace: no memory for buffer, tracing disabled\n");
      trace_enabled = false;
      return;
    }
  tsc0 = rdtsc ();
  tick0 = timer_ticks ();
  trace_thread_name (thread_tid (), thread_name ());
}

/* Remembers NAME for thread TID, for the dump. */
void
trace_thread_name (int tid, const char *name)
{
  if (trace_enabled)
    strlcpy (names[tid % NAME_SLOTS], name, sizeof names[0]);
}

/* Appends one record, overwriting the oldest when the ring is full. */
void
trace_record (enum trace_type type, uint32_t arg)
{
  enum intr_level old_level;
  struct trace_rec *r;

  if (ring == NULL)
    return;
  old_level = intr_disable ();
  r = &ring[next++ % TRACE_CAP];
  r->tsc = rdtsc ();
  r->tid = running_thread_tid ();
  r->type = type;
  r->arg = arg;
  intr_set_level (old_level);
}

/* Prints the buffer over the console (serial), oldest first, in the
   format scripts/trace2perfetto.py reads. */
void
trace_dump (void)
{
  uint32_t first, i;
  int t;

  if (!trace_enabled || ring == NULL)
    return;
  trace_enabled = false;          /* Stop recording our own output. */
  first = next > TRACE_CAP ? next - TRACE_CAP : 0;
  printf ("TRACE-BEGIN records=%u dropped=%u\n",
          (unsigned) (next - first), (unsigned) first);
  printf ("TRACE-CLOCK %llu %lld %llu %lld %d\n",
          tsc0, tick0, rdtsc (), timer_ticks (), TIMER_FREQ);
  for (t = 0; t < NAME_SLOTS; t++)
    if (names[t][0] != '\0')
      printf ("TRACE-THREAD %d %s\n", t, names[t]);
  for (i = first; i < next; i++)
    {
      struct trace_rec *r = &ring[i % TRACE_CAP];
      printf ("TRACE %llu %d %u %u\n", r->tsc, (int) r->tid,
              (unsigned) r->type, (unsigned) r->arg);
    }
  printf ("TRACE-END\n");
}
