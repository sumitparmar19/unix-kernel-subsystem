#ifndef USERPROG_OFILE_H
#define USERPROG_OFILE_H

/* Open-file objects and per-process descriptor tables
   (docs/adr/ADR-005).  A descriptor refers to a reference-counted
   open-file object, which may be shared by several descriptors and
   processes (dup2, exec_redirect). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct file;
struct pipe;

enum ofile_type
  {
    OF_FILE,            /* A file in the file system. */
    OF_PIPE_READ,       /* Read end of a pipe. */
    OF_PIPE_WRITE,      /* Write end of a pipe. */
    OF_CONSOLE_IN,      /* Keyboard / serial input. */
    OF_CONSOLE_OUT      /* Console output. */
  };

struct ofile
  {
    enum ofile_type type;
    int refs;                   /* Descriptors referring to this object. */
    struct file *file;          /* OF_FILE only. */
    struct pipe *pipe;          /* OF_PIPE_* only. */
  };

struct ofile *ofile_console_in (void);
struct ofile *ofile_console_out (void);
struct ofile *ofile_from_file (struct file *);
struct ofile *ofile_from_pipe (struct pipe *, bool write_end);
bool ofile_is_console (const struct ofile *);
struct ofile *ofile_ref (struct ofile *);
void ofile_unref (struct ofile *);

int ofile_read (struct ofile *, void *buffer, unsigned size);
int ofile_write (struct ofile *, const void *buffer, unsigned size);

/* A process's descriptor table. */
struct fdtable
  {
    struct ofile **slots;       /* SLOTS[fd], or NULL if fd is free. */
    int cap;                    /* Number of slots allocated. */
    int open;                   /* Number of slots in use. */
  };

bool fdtable_init (struct fdtable *, struct ofile *in, struct ofile *out);
void fdtable_destroy (struct fdtable *);
struct ofile *fd_lookup (const struct fdtable *, int fd);
int fd_install (struct fdtable *, struct ofile *, int limit);
bool fd_close (struct fdtable *, int fd);
int fd_dup2 (struct fdtable *, int oldfd, int newfd, int limit);

#endif /* userprog/ofile.h */
