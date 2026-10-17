#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/kstat.h"
#include "threads/trace.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/rlimit.h"

/* The user stack may grow to this size below PHYS_BASE (8 MB). */
#define STACK_LIMIT (8 * 1024 * 1024)

/* A fault at most this far below the stack pointer is a stack access
   (the PUSHA instruction touches 32 bytes below esp). */
#define STACK_SLACK 32

static hash_hash_func page_hash;
static hash_less_func page_less;

void
spt_init (struct hash *spt)
{
  hash_init (spt, page_hash, page_less, NULL);
}

static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *p = hash_entry (e, struct spt_entry, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  return hash_entry (a, struct spt_entry, elem)->upage
       < hash_entry (b, struct spt_entry, elem)->upage;
}

/* Returns the current process's entry for UPAGE, or NULL. */
struct spt_entry *
spt_lookup (void *upage)
{
  struct spt_entry key;
  struct hash_elem *e;

  key.upage = pg_round_down (upage);
  e = hash_find (&thread_current ()->spt, &key.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/* Allocates and inserts a new entry for UPAGE.  Returns it, or NULL if
   UPAGE already has an entry or memory runs out. */
static struct spt_entry *
spt_new (void *upage, bool writable)
{
  struct spt_entry *p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;
  p->upage = pg_round_down (upage);
  p->writable = writable;
  p->file = NULL;
  p->file_ofs = 0;
  p->read_bytes = 0;
  p->is_mmap = false;
  p->dirtied = false;
  p->has_swap = false;
  p->swap_slot = 0;
  p->kpage = NULL;
  p->evicting = false;
  p->owner = thread_current ();
  if (hash_insert (&p->owner->spt, &p->elem) != NULL)
    {
      free (p);                 /* Already mapped. */
      return NULL;
    }
  /* Count this user page against the process's page limit (ADR-004). */
  if (!rlimit_charge_pages (p->owner, 1))
    {
      hash_delete (&p->owner->spt, &p->elem);
      kstat_inc (KS_LIMIT_DENIALS);
      free (p);
      return NULL;
    }
  return p;
}

bool
spt_add_file (void *upage, struct file *file, off_t ofs, uint32_t read_bytes,
              bool writable, bool is_mmap)
{
  struct spt_entry *p = spt_new (upage, writable);
  if (p == NULL)
    return false;
  p->file = file;
  p->file_ofs = ofs;
  p->read_bytes = read_bytes;
  p->is_mmap = is_mmap;
  return true;
}

bool
spt_add_zero (void *upage, bool writable)
{
  struct spt_entry *p = spt_new (upage, writable);
  if (p == NULL)
    return false;
  return true;
}

/* Fills a freshly allocated frame KPAGE with P's contents. */
static bool
load_frame (struct spt_entry *p, void *kpage)
{
  if (p->has_swap)
    {
      swap_in (p->swap_slot, kpage);
      p->has_swap = false;
      kstat_inc (KS_SWAP_INS);
      trace_event (TR_SWAP_IN, (uint32_t) p->upage);
      return true;
    }
  if (p->file != NULL)
    {
      off_t n;
      lock_acquire (&filesys_lock);
      file_seek (p->file, p->file_ofs);
      n = file_read (p->file, kpage, p->read_bytes);
      lock_release (&filesys_lock);
      if (n != (off_t) p->read_bytes)
        return false;
      memset ((uint8_t *) kpage + p->read_bytes, 0, PGSIZE - p->read_bytes);
      return true;
    }
  memset (kpage, 0, PGSIZE);     /* Anonymous, never written. */
  return true;
}

/* Brings P into memory and installs its mapping.  If KEEP_PINNED, the
   frame stays pinned (for a system call); otherwise it is unpinned. */
static bool
spt_load (struct spt_entry *p, bool keep_pinned)
{
  void *kpage;

  /* If this page is being evicted right now, wait for the evictor to
     finish recording where its data went; otherwise we could load a
     stale or zero-filled copy over data still on its way to swap. */
  frame_await_eviction (p);

  if (p->kpage != NULL)
    {
      if (keep_pinned)
        frame_pin (p->kpage);
      return true;              /* Already resident. */
    }
  kpage = frame_alloc (p);      /* Returns pinned. */
  if (kpage == NULL)
    return false;
  if (!load_frame (p, kpage))
    {
      frame_free (kpage);
      return false;
    }
  if (!pagedir_set_page (p->owner->pagedir, p->upage, kpage, p->writable))
    {
      frame_free (kpage);
      return false;
    }
  p->kpage = kpage;
  pagedir_set_dirty (p->owner->pagedir, p->upage, false);
  pagedir_set_accessed (p->owner->pagedir, p->upage, true);
  if (!keep_pinned)
    frame_unpin (kpage);
  return true;
}

/* True if a fault at FAULT_ADDR with stack pointer ESP is a valid
   stack growth. */
static bool
is_stack_access (void *fault_addr, void *esp)
{
  return fault_addr >= (void *) ((uint8_t *) esp - STACK_SLACK)
      && fault_addr >= (void *) (PHYS_BASE - STACK_LIMIT)
      && fault_addr < PHYS_BASE;
}

bool
page_fault_handler (void *fault_addr, void *esp, bool write)
{
  struct spt_entry *p = spt_lookup (fault_addr);

  if (p == NULL)
    {
      if (!is_stack_access (fault_addr, esp))
        return false;
      if (!spt_add_zero (pg_round_down (fault_addr), true))
        return false;
      p = spt_lookup (fault_addr);
    }
  if (write && !p->writable)
    return false;
  return spt_load (p, false);
}

/* Writes P's frame out and marks it non-resident.  Called by the
   frame evictor with the frame pinned. */
void
page_evict (struct spt_entry *p, void *kpage)
{
  uint32_t *pd = p->owner->pagedir;
  bool dirty = pagedir_is_dirty (pd, p->upage);

  pagedir_clear_page (pd, p->upage);    /* Further access now faults. */
  p->kpage = NULL;

  if (p->is_mmap)
    {
      if (dirty)
        {
          lock_acquire (&filesys_lock);
          file_write_at (p->file, kpage, p->read_bytes, p->file_ofs);
          lock_release (&filesys_lock);
          kstat_inc (KS_DIRTY_WRITEBACKS);
        }
      /* mmap pages keep their file backing and are re-read on fault. */
    }
  else if (dirty || p->dirtied)
    {
      /* The page holds written data with no other home: preserve it in
         swap, now and on every future eviction. */
      p->dirtied = true;
      p->swap_slot = swap_out (kpage);
      p->has_swap = true;
      kstat_inc (KS_SWAP_OUTS);
      trace_event (TR_SWAP_OUT, (uint32_t) p->upage);
    }
  /* else: a clean page that can be reconstructed from its file (code,
     read-only or unmodified data) or re-zeroed (untouched anon); just
     drop it. */
  kstat_inc (KS_EVICTIONS);
  trace_event (TR_EVICT, (uint32_t) p->upage);
}

/* Pins every page of [UADDR, UADDR+SIZE) for a system call, bringing
   them in (and growing the stack) as needed.  Returns false, pinning
   nothing extra, if any page is invalid or read-only when WRITE. */
bool
page_pin_range (const void *uaddr, size_t size, void *esp, bool write)
{
  uint8_t *a, *last;

  if (size == 0)
    return true;
  last = (uint8_t *) uaddr + size - 1;
  for (a = pg_round_down (uaddr); a <= last; a += PGSIZE)
    {
      struct spt_entry *p = spt_lookup (a);
      if (p == NULL)
        {
          if (!is_stack_access (a, esp) || !spt_add_zero (a, true))
            goto fail;
          p = spt_lookup (a);
        }
      if (write && !p->writable)
        goto fail;
      if (!spt_load (p, true))          /* Load pinned. */
        goto fail;
    }
  return true;

 fail:
  page_unpin_range (uaddr, (uint8_t *) a - (uint8_t *) uaddr);
  return false;
}

void
page_unpin_range (const void *uaddr, size_t size)
{
  uint8_t *a, *last;

  if (size == 0)
    return;
  last = (uint8_t *) uaddr + size - 1;
  for (a = pg_round_down (uaddr); a <= last; a += PGSIZE)
    {
      struct spt_entry *p = spt_lookup (a);
      if (p != NULL && p->kpage != NULL)
        frame_unpin (p->kpage);
    }
}

/* Frees one entry and its resources (frame, swap slot). */
static void
page_free (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *p = hash_entry (e, struct spt_entry, elem);

  if (p->kpage != NULL)
    {
      if (p->owner->pagedir != NULL)
        pagedir_clear_page (p->owner->pagedir, p->upage);
      frame_free (p->kpage);
    }
  else if (p->has_swap)
    swap_free (p->swap_slot);
  rlimit_uncharge_pages (p->owner, 1);
  free (p);
}

void
spt_destroy (struct hash *spt)
{
  hash_destroy (spt, page_free);
}
