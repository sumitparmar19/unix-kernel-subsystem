#include "userprog/umem.h"
#include <stdint.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

/* Returns true if user page UPAGE (page-aligned) may be accessed by
   the kernel on behalf of the current process: below PHYS_BASE,
   mapped in its page directory, and writable if WRITE. */
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

/* Returns true if every page of the SIZE bytes starting at user
   address UADDR passes page_ok().  An empty range is valid: the
   kernel will not touch it. */
bool
validate_range (const void *uaddr, size_t size, bool write)
{
  uintptr_t start = (uintptr_t) uaddr;
  uintptr_t last;
  uintptr_t page;

  if (size == 0)
    return true;
  last = start + size - 1;
  if (last < start || !is_user_vaddr ((const void *) last))
    return false;           /* Wraps around or reaches kernel memory. */

  for (page = (uintptr_t) pg_round_down (uaddr); ; page += PGSIZE)
    {
      if (!page_ok ((const void *) page, write))
        return false;
      if (page == (uintptr_t) pg_round_down ((const void *) last))
        return true;
    }
}

/* Validates the NUL-terminated user string USTR, scanning at most
   MAX bytes.  Checks each page the scan enters before reading it.
   Returns the string's length, UMEM_FAULT if it runs into invalid
   memory, or UMEM_TOO_LONG if there is no NUL in the first MAX
   bytes. */
int
validate_string (const char *ustr, size_t max)
{
  size_t i;

  for (i = 0; i < max; i++)
    {
      const char *p = ustr + i;
      if ((i == 0 || pg_ofs (p) == 0) && !page_ok (pg_round_down (p), false))
        return UMEM_FAULT;
      if (*p == '\0')
        return (int) i;
    }
  return UMEM_TOO_LONG;
}

/* Copies SIZE bytes from user address USRC to kernel buffer KDST.
   Returns false, copying nothing, if the user range is invalid. */
bool
copy_in (void *kdst, const void *usrc, size_t size)
{
  if (!validate_range (usrc, size, false))
    return false;
  memcpy (kdst, usrc, size);
  return true;
}

/* Copies SIZE bytes from kernel buffer KSRC to user address UDST.
   Returns false, copying nothing, if the user range is invalid or
   read-only. */
bool
copy_out (void *udst, const void *ksrc, size_t size)
{
  if (!validate_range (udst, size, true))
    return false;
  memcpy (udst, ksrc, size);
  return true;
}

/* Copies user string USTR, including its NUL, into the SIZE-byte
   kernel buffer KDST.  Returns its length, UMEM_FAULT, or
   UMEM_TOO_LONG if it does not fit. */
int
copy_in_string (char *kdst, const char *ustr, size_t size)
{
  int len = validate_string (ustr, size);
  if (len >= 0)
    memcpy (kdst, ustr, len + 1);
  return len;
}
