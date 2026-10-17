#ifndef VM_PAGE_H
#define VM_PAGE_H

/* Supplemental page table (docs/adr/ADR-003): one per process, it
   records what *should* be at each user page and where its contents
   live, so pages can be loaded lazily and evicted to swap or a file. */

#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "filesys/off_t.h"

struct spt_entry
  {
    void *upage;                /* User page (key), page-aligned. */
    bool writable;              /* May the process write it? */

    /* File backing (NULL for a pure anonymous page). */
    struct file *file;
    off_t file_ofs;
    uint32_t read_bytes;        /* Bytes read from FILE; the rest zero-filled. */
    bool is_mmap;               /* Written back to FILE on eviction if dirty. */

    bool dirtied;               /* Sticky: the page has held written data, so it
                                   must be preserved in swap, never reconstructed. */
    bool has_swap;              /* Contents currently live in SWAP_SLOT. */
    size_t swap_slot;

    void *kpage;                /* Resident frame, or NULL. */
    bool evicting;              /* Being written out right now: the owner must
                                   wait before loading it (avoids racing the
                                   half-updated entry mid-eviction). */
    struct thread *owner;       /* Process that owns this page. */
    struct hash_elem elem;      /* In the owner's spt. */
  };

void spt_init (struct hash *spt);
void spt_destroy (struct hash *spt);

struct spt_entry *spt_lookup (void *upage);
bool spt_add_file (void *upage, struct file *, off_t, uint32_t read_bytes,
                   bool writable, bool is_mmap);
bool spt_add_zero (void *upage, bool writable);

/* Top-level fault handler: brings in the page for FAULT_ADDR, growing
   the stack if appropriate (ESP is the user stack pointer).  Returns
   true if the access can now proceed. */
bool page_fault_handler (void *fault_addr, void *esp, bool write);

/* Called by the frame evictor to write P's frame out and mark it
   non-resident. */
void page_evict (struct spt_entry *p, void *kpage);

/* For umem: bring a user range in and pin its frames for the duration
   of a system call, then release them (docs/adr/ADR-003 §deadlock). */
bool page_pin_range (const void *uaddr, size_t size, void *esp, bool write);
void page_unpin_range (const void *uaddr, size_t size);

#endif /* vm/page.h */
