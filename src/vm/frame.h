#ifndef VM_FRAME_H
#define VM_FRAME_H

/* Frame table: tracks the physical user pages (frames) in use and
   evicts one when memory runs low (docs/adr/ADR-003). */

#include <stdbool.h>

struct spt_entry;

/* Eviction policies, selectable at boot with -evict=NAME. */
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

/* Atomically (under the frame lock): wait out any in-progress eviction
   of P, then, if P is resident, pin its frame and return true; else
   return false.  One lock hold closes the check-then-pin race, so a
   caller never treats a just-evicted page as resident (ADR-003). */
bool frame_pin_resident (struct spt_entry *p);

/* Teardown: wait out any in-progress eviction of P, then free P's
   resident frame (if any) and clear its mapping, leaving p->kpage NULL.
   Only the evictor may free a frame it is evicting; owners wait for it
   here first (ADR-003).  Does not touch swap. */
void frame_release (struct spt_entry *p);

#endif /* vm/frame.h */
