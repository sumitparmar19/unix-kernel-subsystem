#ifndef USERPROG_PIPE_H
#define USERPROG_PIPE_H

/* Pipes: a one-page ring buffer shared by a read end and a write end
   (docs/adr/ADR-005). */

#include <stdbool.h>

struct pipe;

struct pipe *pipe_create (void);
int pipe_read (struct pipe *, void *buffer, unsigned size);
int pipe_write (struct pipe *, const void *buffer, unsigned size);
void pipe_close (struct pipe *, bool write_end);

#endif /* userprog/pipe.h */
