#include "userprog/ofile.h"
#include <stdio.h>
#include <string.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "userprog/pipe.h"
#include "userprog/syscall.h"

/* Console objects shared by every process.  They are never freed, so
   their reference counts are not maintained. */
static struct ofile console_in = { OF_CONSOLE_IN, 1, NULL, NULL };
static struct ofile console_out = { OF_CONSOLE_OUT, 1, NULL, NULL };

/* Returns true if OF is one of the two console objects. */
bool
ofile_is_console (const struct ofile *of)
{
  return of == &console_in || of == &console_out;
}

struct ofile *
ofile_console_in (void)
{
  return &console_in;
}

struct ofile *
ofile_console_out (void)
{
  return &console_out;
}

/* Returns a new open-file object with one reference, or NULL. */
static struct ofile *
ofile_create (enum ofile_type type)
{
  struct ofile *of = malloc (sizeof *of);
  if (of != NULL)
    {
      of->type = type;
      of->refs = 1;
      of->file = NULL;
      of->pipe = NULL;
    }
  return of;
}

/* Wraps FILE in a new object.  Returns NULL (leaving FILE open) if
   out of memory. */
struct ofile *
ofile_from_file (struct file *file)
{
  struct ofile *of = ofile_create (OF_FILE);
  if (of != NULL)
    of->file = file;
  return of;
}

/* Wraps one end of PIPE in a new object.  The pipe already counts
   this end (pipe_create() starts with one of each). */
struct ofile *
ofile_from_pipe (struct pipe *pipe, bool write_end)
{
  struct ofile *of = ofile_create (write_end ? OF_PIPE_WRITE : OF_PIPE_READ);
  if (of != NULL)
    of->pipe = pipe;
  return of;
}

/* Adds a reference to OF and returns it.  Objects may be shared
   between processes, so the count is changed with interrupts off. */
struct ofile *
ofile_ref (struct ofile *of)
{
  if (!ofile_is_console (of))
    {
      enum intr_level old_level = intr_disable ();
      of->refs++;
      intr_set_level (old_level);
    }
  return of;
}

/* Drops a reference to OF, closing the file or pipe end when the
   last one goes. */
void
ofile_unref (struct ofile *of)
{
  enum intr_level old_level;
  bool last;

  if (of == NULL || ofile_is_console (of))
    return;
  old_level = intr_disable ();
  last = --of->refs == 0;
  intr_set_level (old_level);
  if (!last)
    return;

  if (of->type == OF_FILE)
    {
      lock_acquire (&filesys_lock);
      file_close (of->file);
      lock_release (&filesys_lock);
    }
  else if (of->type == OF_PIPE_READ || of->type == OF_PIPE_WRITE)
    pipe_close (of->pipe, of->type == OF_PIPE_WRITE);
  free (of);
}

/* Reads up to SIZE bytes from OF into BUFFER (already validated by
   the caller).  Returns the number read, or -1 if OF is not
   readable. */
int
ofile_read (struct ofile *of, void *buffer, unsigned size)
{
  switch (of->type)
    {
    case OF_CONSOLE_IN:
      {
        uint8_t *p = buffer;
        unsigned i;
        for (i = 0; i < size; i++)
          p[i] = input_getc ();
        return size;
      }
    case OF_FILE:
      {
        int n;
        lock_acquire (&filesys_lock);
        n = file_read (of->file, buffer, size);
        lock_release (&filesys_lock);
        return n;
      }
    case OF_PIPE_READ:
      return pipe_read (of->pipe, buffer, size);
    default:
      return -1;
    }
}

/* Writes SIZE bytes from BUFFER (already validated) to OF.  Returns
   the number written, or -1 if OF is not writable. */
int
ofile_write (struct ofile *of, const void *buffer, unsigned size)
{
  switch (of->type)
    {
    case OF_CONSOLE_OUT:
      putbuf (buffer, size);
      return size;
    case OF_FILE:
      {
        int n;
        lock_acquire (&filesys_lock);
        n = file_write (of->file, buffer, size);
        lock_release (&filesys_lock);
        return n;
      }
    case OF_PIPE_WRITE:
      return pipe_write (of->pipe, buffer, size);
    default:
      return -1;
    }
}

/* Initial number of descriptor slots. */
#define FDTABLE_INITIAL_CAP 16

/* Initializes T with IN as fd 0 and OUT as fd 1, taking over the
   caller's references to both.  Returns false if out of memory, in
   which case the references are dropped. */
bool
fdtable_init (struct fdtable *t, struct ofile *in, struct ofile *out)
{
  t->slots = calloc (FDTABLE_INITIAL_CAP, sizeof *t->slots);
  if (t->slots == NULL)
    {
      t->cap = t->open = 0;
      ofile_unref (in);
      ofile_unref (out);
      return false;
    }
  t->cap = FDTABLE_INITIAL_CAP;
  t->slots[0] = in;
  t->slots[1] = out;
  t->open = 2;
  return true;
}

/* Closes every descriptor in T and frees the table. */
void
fdtable_destroy (struct fdtable *t)
{
  int fd;

  for (fd = 0; fd < t->cap; fd++)
    if (t->slots[fd] != NULL)
      ofile_unref (t->slots[fd]);
  free (t->slots);
  t->slots = NULL;
  t->cap = t->open = 0;
}

/* Returns the object for FD, or NULL if FD is not open. */
struct ofile *
fd_lookup (const struct fdtable *t, int fd)
{
  if (fd < 0 || fd >= t->cap)
    return NULL;
  return t->slots[fd];
}

/* Grows T so that descriptor FD has a slot.  Returns false if out
   of memory. */
static bool
fdtable_reserve (struct fdtable *t, int fd)
{
  struct ofile **slots;
  int cap = t->cap > 0 ? t->cap : FDTABLE_INITIAL_CAP;

  if (fd < t->cap)
    return true;
  while (cap <= fd)
    {
      if (cap > (1 << 20))
        return false;
      cap *= 2;
    }
  slots = realloc (t->slots, cap * sizeof *slots);
  if (slots == NULL)
    return false;
  memset (slots + t->cap, 0, (cap - t->cap) * sizeof *slots);
  t->slots = slots;
  t->cap = cap;
  return true;
}

/* Installs OF at the lowest free descriptor below LIMIT, taking over
   the caller's reference.  Returns the descriptor, or -1 (leaving the
   reference with the caller) if none is free or memory runs out. */
int
fd_install (struct fdtable *t, struct ofile *of, int limit)
{
  int fd;

  for (fd = 0; fd < limit; fd++)
    {
      if (fd >= t->cap && !fdtable_reserve (t, fd))
        return -1;
      if (t->slots[fd] == NULL)
        {
          t->slots[fd] = of;
          t->open++;
          return fd;
        }
    }
  return -1;
}

/* Closes descriptor FD.  Returns false if it was not open. */
bool
fd_close (struct fdtable *t, int fd)
{
  struct ofile *of = fd_lookup (t, fd);

  if (of == NULL)
    return false;
  t->slots[fd] = NULL;
  t->open--;
  ofile_unref (of);
  return true;
}

/* POSIX dup2(): makes NEWFD refer to OLDFD's object, closing NEWFD
   first if it is open.  Returns NEWFD, or -1 if OLDFD is not open,
   NEWFD is outside [0, LIMIT), or memory runs out. */
int
fd_dup2 (struct fdtable *t, int oldfd, int newfd, int limit)
{
  struct ofile *of = fd_lookup (t, oldfd);

  if (of == NULL || newfd < 0 || newfd >= limit)
    return -1;
  if (oldfd == newfd)
    return newfd;
  if (!fdtable_reserve (t, newfd))
    return -1;
  if (t->slots[newfd] != NULL)
    fd_close (t, newfd);
  t->slots[newfd] = ofile_ref (of);
  t->open++;
  return newfd;
}
