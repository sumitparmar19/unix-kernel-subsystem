#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Sectors per page. */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_device;   /* The swap disk, or NULL if none. */
static struct bitmap *used_slots;   /* One bit per page-sized slot; 1 = in use. */
static struct lock swap_lock;       /* Protects used_slots. */

/* Finds and initializes the swap device, if present. */
void
swap_init (void)
{
  lock_init (&swap_lock);
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    {
      used_slots = NULL;
      return;
    }
  used_slots = bitmap_create (block_size (swap_device) / SECTORS_PER_PAGE);
  if (used_slots == NULL)
    PANIC ("swap: cannot create slot bitmap");
}

/* Writes the page at PAGE to a free swap slot and returns the slot
   number.  Panics if swap is full or absent. */
size_t
swap_out (const void *page)
{
  size_t slot, i;

  ASSERT (pg_ofs (page) == 0);
  if (swap_device == NULL)
    PANIC ("swap: no swap device configured but a page needs evicting");

  lock_acquire (&swap_lock);
  slot = bitmap_scan_and_flip (used_slots, 0, 1, false);
  lock_release (&swap_lock);
  if (slot == BITMAP_ERROR)
    PANIC ("swap: out of swap slots");

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (swap_device, slot * SECTORS_PER_PAGE + i,
                 (const uint8_t *) page + i * BLOCK_SECTOR_SIZE);
  return slot;
}

/* Reads SLOT back into PAGE and frees the slot. */
void
swap_in (size_t slot, void *page)
{
  size_t i;

  ASSERT (pg_ofs (page) == 0);
  ASSERT (swap_device != NULL);

  /* Read the data out *before* freeing the slot.  Freeing first would let
     a concurrent swap_out grab this slot and overwrite it between the
     reset and the read, handing us another page's bytes (observed as
     stack/return-address corruption -- eip=0 -- under parallel paging). */
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_device, slot * SECTORS_PER_PAGE + i,
                (uint8_t *) page + i * BLOCK_SECTOR_SIZE);

  lock_acquire (&swap_lock);
  ASSERT (bitmap_test (used_slots, slot));
  bitmap_reset (used_slots, slot);
  lock_release (&swap_lock);
}

/* Releases SLOT without reading it (the owning page is gone). */
void
swap_free (size_t slot)
{
  if (swap_device == NULL)
    return;
  lock_acquire (&swap_lock);
  ASSERT (bitmap_test (used_slots, slot));
  bitmap_reset (used_slots, slot);
  lock_release (&swap_lock);
}
