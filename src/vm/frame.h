#ifndef VM_FRAME_H
#define VM_FRAME_H

/* Frame table: tracks the physical user pages (frames) in use and
   evicts one when memory runs low (docs/adr/ADR-003). */

#include <stdbool.h>

struct spt_entry;

/* Eviction policies, selectable at boot with -evict=NAME (M7). */
enum evict_policy { EVICT_CLOCK, EVICT_FIFO, EVICT_RANDOM };
extern enum evict_policy frame_policy;

void frame_init (void);

/* Allocates a frame for page P (its spt_entry).  Evicts another frame
   if memory is full.  The returned frame is pinned; the caller must
   unpin it once P's mapping is installed.  Returns the kernel virtual
   address of the frame, or NULL only if eviction is impossible. */
void *frame_alloc (struct spt_entry *p);

void frame_free (void *kpage);        /* Frees a frame (page is going away). */
void frame_pin (void *kpage);         /* Protects a frame from eviction. */
void frame_unpin (void *kpage);

/* Blocks while P's frame is mid-eviction, so the owner never loads a
   page whose contents are still being written out (ADR-003). */
void frame_await_eviction (struct spt_entry *p);

#endif /* vm/frame.h */
