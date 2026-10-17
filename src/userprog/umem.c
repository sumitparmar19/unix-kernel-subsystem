#include "userprog/umem.h"
#include <stdint.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#ifdef VM
#include "vm/page.h"
#endif

#ifdef VM
/* The user stack pointer saved on entry to the current system call,
   used to decide whether a fault is a valid stack growth. */
static void *
cur_esp (void)
{
  return thread_current ()->user_esp;
}
#endif

#ifndef VM
/* Without VM, a user page is accessible iff it is below PHYS_BASE,
   present in the page directory, and writable when WRITE. */
static bool
page_ok (const void *upage, bool write)
{
  uint32_t *pd = thread_current ()->pagedir;

  if (!is_user_vaddr (upage) || pd == NULL)
    return false;
  if (pagedir_get_page (pd, upage) == NULL)
    return false;
  return !write || pagedir_is_writable (pd, upage);
}
#endif

/* Returns true if every page of the SIZE bytes at UADDR is a valid
   user access (WRITE means the kernel will write it).  Under VM the
   pages are brought in and pinned, then released, so this only
   reports whether the access *can* succeed. */
bool
validate_range (const void *uaddr, size_t size, bool write)
{
  uintptr_t start = (uintptr_t) uaddr, last;

  if (size == 0)
    return true;
  last = start + size - 1;
  if (last < start || !is_user_vaddr ((const void *) last))
    return false;
#ifdef VM
  if (!page_pin_range (uaddr, size, cur_esp (), write))
    return false;
  page_unpin_range (uaddr, size);
  return true;
#else
  {
    uintptr_t page;
    for (page = (uintptr_t) pg_round_down (uaddr); ; page += PGSIZE)
      {
        if (!page_ok ((const void *) page, write))
          return false;
        if (page == (uintptr_t) pg_round_down ((const void *) last))
          return true;
      }
  }
#endif
}

/* Validates the NUL-terminated user string USTR (at most MAX bytes),
   checking each page it enters.  Returns its length, UMEM_FAULT, or
   UMEM_TOO_LONG. */
int
validate_string (const char *ustr, size_t max)
{
  size_t i;

  for (i = 0; i < max; i++)
    {
      const char *p = ustr + i;
      bool boundary = (i == 0 || pg_ofs (p) == 0);
#ifdef VM
      if (boundary && !page_pin_range (pg_round_down (p), 1, cur_esp (), false))
        { page_unpin_range (ustr, i); return UMEM_FAULT; }
      if (*p == '\0')
        { page_unpin_range (ustr, i + 1); return (int) i; }
#else
      if (boundary && !page_ok (pg_round_down (p), false))
        return UMEM_FAULT;
      if (*p == '\0')
        return (int) i;
#endif
    }
#ifdef VM
  page_unpin_range (ustr, max);
#endif
  return UMEM_TOO_LONG;
}

/* Copies SIZE bytes from user USRC to kernel KDST, or returns false. */
bool
copy_in (void *kdst, const void *usrc, size_t size)
{
#ifdef VM
  if (size == 0)
    return true;
  if (!page_pin_range (usrc, size, cur_esp (), false))
    return false;
  memcpy (kdst, usrc, size);
  page_unpin_range (usrc, size);
  return true;
#else
  if (!validate_range (usrc, size, false))
    return false;
  memcpy (kdst, usrc, size);
  return true;
#endif
}

/* Copies SIZE bytes from kernel KSRC to user UDST, or returns false. */
bool
copy_out (void *udst, const void *ksrc, size_t size)
{
#ifdef VM
  if (size == 0)
    return true;
  if (!page_pin_range (udst, size, cur_esp (), true))
    return false;
  memcpy (udst, ksrc, size);
  page_unpin_range (udst, size);
  return true;
#else
  if (!validate_range (udst, size, true))
    return false;
  memcpy (udst, ksrc, size);
  return true;
#endif
}

/* Copies user string USTR (with its NUL) into the SIZE-byte buffer
   KDST.  Returns its length, UMEM_FAULT, or UMEM_TOO_LONG. */
int
copy_in_string (char *kdst, const char *ustr, size_t size)
{
#ifdef VM
  size_t i;
  for (i = 0; i < size; i++)
    {
      const char *p = ustr + i;
      if ((i == 0 || pg_ofs (p) == 0)
          && !page_pin_range (pg_round_down (p), 1, cur_esp (), false))
        { page_unpin_range (ustr, i); return UMEM_FAULT; }
      kdst[i] = *p;
      if (*p == '\0')
        { page_unpin_range (ustr, i + 1); return (int) i; }
    }
  page_unpin_range (ustr, size);
  return UMEM_TOO_LONG;
#else
  int len = validate_string (ustr, size);
  if (len >= 0)
    memcpy (kdst, ustr, len + 1);
  return len;
#endif
}
