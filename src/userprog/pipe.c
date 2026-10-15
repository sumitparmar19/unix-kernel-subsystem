#include "userprog/pipe.h"
#include <stdint.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/kstat.h"
#include "threads/trace.h"

/* Capacity of a pipe in bytes. */
#define PIPE_SIZE PGSIZE

struct pipe
  {
    struct lock lock;           /* Protects every member below. */
    struct condition readable;  /* Signalled when data arrives or the
                                   last writer leaves. */
    struct condition writable;  /* Signalled when space frees up or the
                                   last reader leaves. */
    uint8_t *buf;               /* PIPE_SIZE-byte ring buffer. */
    size_t head;                /* Offset of the oldest byte. */
    size_t len;                 /* Bytes currently buffered. */
    int readers;                /* Open read-end objects. */
    int writers;                /* Open write-end objects. */
  };

/* Creates a pipe with one read end and one write end open.  Returns
   NULL if out of memory. */
struct pipe *
pipe_create (void)
{
  struct pipe *p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;
  p->buf = palloc_get_page (0);
  if (p->buf == NULL)
    {
      free (p);
      return NULL;
    }
  lock_init (&p->lock);
  cond_init (&p->readable);
  cond_init (&p->writable);
  p->head = p->len = 0;
  p->readers = p->writers = 1;
  return p;
}

/* Reads up to SIZE bytes into BUFFER.  Blocks while the pipe is
   empty and a writer remains.  Returns the number of bytes read,
   which is 0 at end of file (empty and no writers). */
int
pipe_read (struct pipe *p, void *buffer, unsigned size)
{
  uint8_t *dst = buffer;
  size_t n, first;

  if (size == 0)
    return 0;
  lock_acquire (&p->lock);
  while (p->len == 0 && p->writers > 0)
    {
      kstat_inc (KS_PIPE_BLOCKS);
      trace_event (TR_PIPE_BLOCK, 0);
      cond_wait (&p->readable, &p->lock);
      trace_event (TR_PIPE_WAKE, 0);
    }

  n = size < p->len ? size : p->len;
  first = n < PIPE_SIZE - p->head ? n : PIPE_SIZE - p->head;
  memcpy (dst, p->buf + p->head, first);
  memcpy (dst + first, p->buf, n - first);
  p->head = (p->head + n) % PIPE_SIZE;
  p->len -= n;

  if (n > 0)
    cond_broadcast (&p->writable, &p->lock);
  lock_release (&p->lock);
  return n;
}

/* Writes all SIZE bytes from BUFFER, blocking while the pipe is full
   and a reader remains.  Returns SIZE, or -1 if there is no reader
   (POSIX would also raise SIGPIPE).  If the last reader leaves
   partway, returns the number of bytes written before that. */
int
pipe_write (struct pipe *p, const void *buffer, unsigned size)
{
  const uint8_t *src = buffer;
  size_t written = 0;

  lock_acquire (&p->lock);
  if (p->readers == 0)
    {
      lock_release (&p->lock);
      return -1;
    }
  while (written < size)
    {
      size_t tail, chunk, first;

      while (p->len == PIPE_SIZE && p->readers > 0)
        {
          kstat_inc (KS_PIPE_BLOCKS);
          trace_event (TR_PIPE_BLOCK, 1);
          cond_wait (&p->writable, &p->lock);
          trace_event (TR_PIPE_WAKE, 1);
        }
      if (p->readers == 0)
        break;

      tail = (p->head + p->len) % PIPE_SIZE;
      chunk = size - written;
      if (chunk > PIPE_SIZE - p->len)
        chunk = PIPE_SIZE - p->len;
      first = chunk < PIPE_SIZE - tail ? chunk : PIPE_SIZE - tail;
      memcpy (p->buf + tail, src + written, first);
      memcpy (p->buf, src + written + first, chunk - first);
      p->len += chunk;
      written += chunk;
      kstat[KS_PIPE_BYTES] += chunk;
      cond_broadcast (&p->readable, &p->lock);
    }
  lock_release (&p->lock);
  return written;
}

/* Closes one end of P (the write end if WRITE_END) and wakes any
   blocked peer.  Frees P when both ends are closed. */
void
pipe_close (struct pipe *p, bool write_end)
{
  bool dead;

  lock_acquire (&p->lock);
  if (write_end)
    p->writers--;
  else
    p->readers--;
  cond_broadcast (&p->readable, &p->lock);
  cond_broadcast (&p->writable, &p->lock);
  dead = p->readers == 0 && p->writers == 0;
  lock_release (&p->lock);

  if (dead)
    {
      palloc_free_page (p->buf);
      free (p);
    }
}
