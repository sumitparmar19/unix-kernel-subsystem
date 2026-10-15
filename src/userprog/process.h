#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/ofile.h"

/* Shared record between a parent and one child process
   (docs/adr/ADR-002).  The parent allocates it before the child
   exists; parent and child each hold one reference and drop it
   exactly once with child_record_release().  The last one to drop
   it frees it. */
struct child_record
  {
    tid_t tid;                  /* Child's tid (set by the parent). */
    int exit_status;            /* Valid once wait_sema is up. */
    bool load_ok;               /* Valid once load_sema is up. */
    int refs;                   /* References left (starts at 2). */
    struct semaphore load_sema; /* Child -> parent: load finished. */
    struct semaphore wait_sema; /* Child -> parent: child exited. */
    struct list_elem elem;      /* In the parent's `children' list. */
  };

tid_t process_execute (const char *cmd_line);
tid_t process_execute_redirect (const char *cmd_line, struct ofile *stdin_of,
                                struct ofile *stdout_of);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
