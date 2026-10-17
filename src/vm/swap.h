#ifndef VM_SWAP_H
#define VM_SWAP_H

/* Swap: stores evicted anonymous pages on the swap block device
   (docs/adr/ADR-003).  A slot holds one page. */

#include <stdbool.h>
#include <stddef.h>

#define SWAP_ERROR ((size_t) -1)

void swap_init (void);
size_t swap_out (const void *page);   /* Writes PAGE out; returns its slot. */
void swap_in (size_t slot, void *page);   /* Reads SLOT back into PAGE, frees slot. */
void swap_free (size_t slot);             /* Releases a slot without reading it. */

#endif /* vm/swap.h */
