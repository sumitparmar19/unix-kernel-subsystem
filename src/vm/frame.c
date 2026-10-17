#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include <random.h>
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
static struct condition frame_cond;    /* Signalled when an eviction finishes. */

/* Selected eviction policy (default clock); set by -evict= in init.c. */
enum evict_policy frame_policy = EVICT_CLOCK;

void
frame_init (void)
{
  list_init (&frame_list);
  clock_hand = NULL;
  lock_init (&frame_lock);
  cond_init (&frame_cond);
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

/* Returns the first unpinned frame at or after the clock hand, by
   list order (FIFO: oldest allocated).  frame_lock held. */
static struct frame *
first_unpinned (void)
{
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (!f->pinned)
        return f;
    }
  return NULL;
}

/* Returns a pseudo-random unpinned frame.  frame_lock held. */
static struct frame *
random_unpinned (void)
{
  size_t n = list_size (&frame_list), i, start = random_ulong () % (n ? n : 1);
  struct list_elem *e = list_begin (&frame_list);
  for (i = 0; i < start; i++)
    e = list_next (e);
  for (i = 0; i < n; i++)
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (!f->pinned)
        return f;
      e = list_next (e);
      if (e == list_end (&frame_list))
        e = list_begin (&frame_list);
    }
  return NULL;
}

/* Picks a victim per the selected policy (clock gives accessed pages a
   second chance), evicts its page, and returns the freed frame.
   frame_lock held. */
static struct frame *
evict (void)
{
  struct frame *f = NULL;

  if (frame_policy == EVICT_CLOCK)
    {
      size_t steps, n = list_size (&frame_list) * 2 + 1;
      for (steps = 0; steps < n; steps++)
        {
          struct frame *c = clock_next ();
          if (c->pinned)
            continue;
          uint32_t *pd = c->page->owner->pagedir;
          if (pagedir_is_accessed (pd, c->page->upage))
            {
              pagedir_set_accessed (pd, c->page->upage, false);
              continue;         /* Second chance. */
            }
          f = c;
          break;
        }
    }
  else if (frame_policy == EVICT_FIFO)
    f = first_unpinned ();
  else
    f = random_unpinned ();

  if (f == NULL)
    PANIC ("frame: no evictable frame found");

  /* Write the victim out with the table unlocked (swap/file I/O must not
     be held under frame_lock).  Mark the page evicting first so that if
     its owner faults on it meanwhile, frame_await_eviction makes it wait
     until the data's new home (swap slot / file) is recorded below. */
  struct spt_entry *victim = f->page;
  f->pinned = true;
  victim->evicting = true;
  lock_release (&frame_lock);
  page_evict (victim, f->kpage);
  lock_acquire (&frame_lock);
  victim->evicting = false;
  cond_broadcast (&frame_cond, &frame_lock);
  f->page = NULL;
  return f;
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

void
frame_await_eviction (struct spt_entry *p)
{
  lock_acquire (&frame_lock);
  while (p->evicting)
    cond_wait (&frame_cond, &frame_lock);
  lock_release (&frame_lock);
}
