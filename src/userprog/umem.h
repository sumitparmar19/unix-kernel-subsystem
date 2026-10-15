#ifndef USERPROG_UMEM_H
#define USERPROG_UMEM_H

/* Safe access to user memory from system calls (docs/adr/ADR-001).
   Every pointer a user program passes to the kernel is validated
   page by page against the process's page tables before the kernel
   touches it.  No other kernel code dereferences user pointers. */

#include <stdbool.h>
#include <stddef.h>

/* Results of validate_string() other than a length. */
#define UMEM_FAULT (-1)         /* Invalid, unmapped or kernel memory. */
#define UMEM_TOO_LONG (-2)      /* No NUL within the allowed length. */

bool validate_range (const void *uaddr, size_t size, bool write);
int validate_string (const char *ustr, size_t max);

bool copy_in (void *kdst, const void *usrc, size_t size);
bool copy_out (void *udst, const void *ksrc, size_t size);
int copy_in_string (char *kdst, const char *ustr, size_t size);

#endif /* userprog/umem.h */
