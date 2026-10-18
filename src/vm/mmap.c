#include "vm/mmap.h"
#include <list.h>
#include <round.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"

struct mmap_region
  {
    mapid_t id;
    void *addr;                 /* First page. */
    int page_cnt;
    struct file *file;          /* Our private reopened handle. */
    struct list_elem elem;
  };

/* Maps FILE (an already-reopened handle this region owns) at ADDR.
   Returns the new mapping id, or MAP_FAILED. */
mapid_t
mmap_create (struct file *file, void *addr)
{
  struct thread *t = thread_current ();
  struct mmap_region *r;
  off_t length;
  int pages, i;

  lock_acquire (&filesys_lock);
  length = file_length (file);
  lock_release (&filesys_lock);
  if (length == 0)
    return MAP_FAILED;
  pages = (length + PGSIZE - 1) / PGSIZE;

  /* Every target page must be free (no overlap with code, data,
     stack or another mapping). */
  for (i = 0; i < pages; i++)
    if (spt_lookup ((uint8_t *) addr + i * PGSIZE) != NULL)
      return MAP_FAILED;

  r = malloc (sizeof *r);
  if (r == NULL)
    return MAP_FAILED;
  r->file = file;
  r->addr = addr;
  r->page_cnt = pages;
  r->id = t->next_mapid++;

  for (i = 0; i < pages; i++)
    {
      void *upage = (uint8_t *) addr + i * PGSIZE;
      off_t ofs = i * PGSIZE;
      uint32_t read_bytes = length - ofs < PGSIZE ? length - ofs : PGSIZE;
      if (!spt_add_file (upage, file, ofs, read_bytes, true, true))
        {
          /* Roll back the pages added so far. */
          while (--i >= 0)
            {
              struct spt_entry *p = spt_lookup ((uint8_t *) addr + i * PGSIZE);
              if (p != NULL)
                {
                  hash_delete (&t->spt, &p->elem);
                  frame_release (p);    /* No-op here (never faulted in yet). */
                  free (p);
                }
            }
          free (r);
          return MAP_FAILED;
        }
    }
  list_push_back (&t->mmaps, &r->elem);
  return r->id;
}

/* Writes back (if dirty) and tears down one mapped page. */
static void
unmap_page (struct thread *t, struct file *file, void *upage)
{
  struct spt_entry *p = spt_lookup (upage);
  if (p == NULL)
    return;
  /* Pin the page if resident, waiting out any in-progress eviction, so it
     can't be evicted or freed under us while we write it back (ADR-003).
     If it is not resident, an evictor already wrote back any dirty data to
     the file, so there is nothing to do. */
  if (frame_pin_resident (p))
    {
      if (pagedir_is_dirty (t->pagedir, upage))
        {
          lock_acquire (&filesys_lock);
          file_write_at (file, p->kpage, p->read_bytes, p->file_ofs);
          lock_release (&filesys_lock);
        }
      pagedir_clear_page (t->pagedir, upage);
      frame_free (p->kpage);
      p->kpage = NULL;
    }
  hash_delete (&t->spt, &p->elem);
  free (p);
}

static struct mmap_region *
find_region (mapid_t id)
{
  struct list_elem *e;
  struct thread *t = thread_current ();
  for (e = list_begin (&t->mmaps); e != list_end (&t->mmaps); e = list_next (e))
    {
      struct mmap_region *r = list_entry (e, struct mmap_region, elem);
      if (r->id == id)
        return r;
    }
  return NULL;
}

bool
mmap_remove (mapid_t id)
{
  struct thread *t = thread_current ();
  struct mmap_region *r = find_region (id);
  int i;

  if (r == NULL)
    return false;
  for (i = 0; i < r->page_cnt; i++)
    unmap_page (t, r->file, (uint8_t *) r->addr + i * PGSIZE);
  list_remove (&r->elem);
  lock_acquire (&filesys_lock);
  file_close (r->file);
  lock_release (&filesys_lock);
  free (r);
  return true;
}

void
mmap_cleanup (void)
{
  struct thread *t = thread_current ();
  while (!list_empty (&t->mmaps))
    {
      struct mmap_region *r = list_entry (list_front (&t->mmaps),
                                          struct mmap_region, elem);
      mmap_remove (r->id);
    }
}
