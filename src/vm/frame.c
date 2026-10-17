#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

/* One physical user frame. */
struct frame
  {
    void *kpage;                /* Kernel virtual address of the frame. */
    struct spt_entry *page;     /* The user page occupying it. */
    bool pinned;                /* If set, never chosen for eviction. */
    struct list_elem elem;      /* In frame_list. */
  };

static struct list frame_list;         /* All allocated frames (clock order). */
static struct list_elem *clock_hand;   /* Next frame the clock will examine. */
static struct lock frame_lock;         /* Protects the table and eviction. */

void
frame_init (void)
{
  list_init (&frame_list);
  clock_hand = NULL;
  lock_init (&frame_lock);
}

/* Returns the frame record holding KPAGE, or NULL.  frame_lock held. */
static struct frame *
frame_for (void *kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->kpage == kpage)
        return f;
    }
  return NULL;
}

/* Advances the clock hand one step (circular). frame_lock held. */
static struct frame *
clock_next (void)
{
  if (clock_hand == NULL || clock_hand == list_end (&frame_list))
    clock_hand = list_begin (&frame_list);
  struct frame *f = list_entry (clock_hand, struct frame, elem);
  clock_hand = list_next (clock_hand);
  return f;
}

/* Chooses a victim by the clock (second-chance) algorithm, evicts its
   page, and returns the now-free frame record, still in the list.
   frame_lock held. */
static struct frame *
evict (void)
{
  size_t steps, n = list_size (&frame_list) * 2 + 1;

  for (steps = 0; steps < n; steps++)
    {
      struct frame *f = clock_next ();
      if (f->pinned)
        continue;
      uint32_t *pd = f->page->owner->pagedir;
      if (pagedir_is_accessed (pd, f->page->upage))
        {
          pagedir_set_accessed (pd, f->page->upage, false);
          continue;             /* Second chance. */
        }
      /* Evict f->page: pin it so nobody else touches it mid-eviction. */
      f->pinned = true;
      lock_release (&frame_lock);
      page_evict (f->page, f->kpage);   /* Swaps out / writes back / drops. */
      lock_acquire (&frame_lock);
      f->page = NULL;
      return f;
    }
  PANIC ("frame: no evictable frame found");
}

void *
frame_alloc (struct spt_entry *p)
{
  void *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  struct frame *f;

  lock_acquire (&frame_lock);
  if (kpage != NULL)
    {
      f = malloc (sizeof *f);
      if (f == NULL)
        {
          palloc_free_page (kpage);
          lock_release (&frame_lock);
          return NULL;
        }
      f->kpage = kpage;
      list_push_back (&frame_list, &f->elem);
    }
  else
    {
      /* Memory full: reuse an evicted frame. */
      f = evict ();
      kpage = f->kpage;
      memset (kpage, 0, PGSIZE);
    }
  f->page = p;
  f->pinned = true;             /* Caller unpins after installing the mapping. */
  lock_release (&frame_lock);
  return kpage;
}

void
frame_free (void *kpage)
{
  struct frame *f;
  lock_acquire (&frame_lock);
  f = frame_for (kpage);
  if (f != NULL)
    {
      if (clock_hand == &f->elem)
        clock_hand = list_next (clock_hand);
      list_remove (&f->elem);
      palloc_free_page (f->kpage);
      free (f);
    }
  lock_release (&frame_lock);
}

static void
set_pin (void *kpage, bool pinned)
{
  struct frame *f;
  lock_acquire (&frame_lock);
  f = frame_for (kpage);
  if (f != NULL)
    f->pinned = pinned;
  lock_release (&frame_lock);
}

void frame_pin (void *kpage) { set_pin (kpage, true); }
void frame_unpin (void *kpage) { set_pin (kpage, false); }
