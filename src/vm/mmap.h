#ifndef VM_MMAP_H
#define VM_MMAP_H

/* Memory-mapped files (docs/adr/ADR-003).  Each region is a run of
   lazily-loaded, file-backed pages; dirty pages are written back to
   the file on unmap or process exit. */

#include <stdbool.h>

typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

struct file;

mapid_t mmap_create (struct file *file, void *addr);
bool mmap_remove (mapid_t);
void mmap_cleanup (void);          /* Unmap all of the current process's regions. */

#endif /* vm/mmap.h */
