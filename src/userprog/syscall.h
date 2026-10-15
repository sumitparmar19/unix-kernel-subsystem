#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

/* Global lock for file system operations.  Defined in syscall.c. */
extern struct lock filesys_lock;

void syscall_init (void);
void syscall_print_stats (void);

#endif /* userprog/syscall.h */

