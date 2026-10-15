#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "filesys/file.h"
#include "lib/kernel/list.h"

/* Project 2: simple FD table shared between syscall.c and process.c. */
struct fd_entry
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

/* Global lock for file system operations.  Defined in syscall.c. */
extern struct lock filesys_lock;

void syscall_init (void);

#endif /* userprog/syscall.h */

