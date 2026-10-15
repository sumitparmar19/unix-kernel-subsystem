#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"

/* Per‑child record, used by process_wait/exit. */
struct child_info
  {
    tid_t tid;
    int exit_status;
    bool finished;
    bool waited;
    struct semaphore wait_sema;
    struct list_elem elem;   /* In parent's children list. */
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
