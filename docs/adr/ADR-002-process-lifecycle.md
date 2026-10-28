# ADR-002: Process lifecycle, child records and file-system locking

**Status:** accepted · **Code:** `src/userprog/process.[ch]`, `src/threads/thread.[ch]`

## Context
The course version (tag `course-submission-2025-11`) had four lifecycle defects, all measured on the course version:

1. `exec` returned the child's pid **before** the child had loaded, so `exec("no-such-file")` returned
   a valid pid (`exec-missing`).
2. A failed load exited with status 0, the default in `init_thread`.
3. The parent created its `child_info` **after** `thread_create` and found the child again with
   `thread_get_by_tid`, which walks `all_list` without protection. A child that exited first was never
   linked, so its parent's `wait` would block forever.
4. A parent that exited without waiting leaked every `child_info` it owned.

Separately, `load` called `filesys_open`/`file_read` **without** `filesys_lock`, and the Pintos base file
system is not thread-safe. Under concurrent `exec`s, children intermittently failed to open their
executable (`syn-read` and `syn-write` fail or pass depending on timing). Running executables were not
write-protected (`rox-*`).

## Decision

### A shared, reference-counted child record
```
struct child_record {          /* userprog/process.h */
  tid_t tid;
  int exit_status;             /* valid once `wait_sema` is up */
  bool load_ok;                /* valid once `load_sema` is up */
  int refs;                    /* 2 while both parent and child hold it */
  struct semaphore load_sema;  /* child -> parent: load finished */
  struct semaphore wait_sema;  /* child -> parent: exited */
  struct list_elem elem;       /* in parent's `children` list */
};
```
- The **parent allocates the record before creating the child** and passes it to the child together
  with the command line (`struct exec_args`). No lookup by tid, so there's no window where the child
  runs without being linked.
- **Two references:** one for the parent, one for the child. Each side calls `child_record_release`
  exactly once; whoever drops the count to 0 frees the record. The count is changed with interrupts
  off (the critical section is a few instructions, and it may run from either thread).
- **Load handshake:** `process_execute` does `sema_down (&rec->load_sema)` after `thread_create`. The
  child sets `load_ok` and does `sema_up` right after `load`. If loading failed, `exec` returns −1, the
  record is never added to the `children` list, and the parent drops its reference.
- **Exit:** a failed load sets `exit_status = -1` before exiting. `process_exit` prints
  `name: exit(status)` **first**, then frees the process's resources (files, executable, page
  directory), **then** publishes the status and does `sema_up (&wait_sema)`, then drops the child's
  reference. Freeing before signalling means a parent that wakes from `wait` sees the child's memory
  already returned (`multi-oom`).
- **Wait:** finds the record in `children` (only the parent's own children are there), `sema_down`s
  `wait_sema`, takes the status, removes the record and drops the parent's reference. A second `wait`,
  or a `wait` on a non-child, returns −1 because the record is no longer in the list.
- **Orphans:** when a parent exits, it drops its reference on every record still in its `children`
  list. A child that is still running keeps its own reference, so the record stays valid until the child
  exits.
- Default `exit_status` is −1, so any path that ends a process without calling `exit` (a fault, a
  kill) reports −1.

### File-system locking
- `filesys_lock` (global, in `syscall.c`) serialises **every** call into `filesys/`, including the
  ones in `load` (open, read headers and segments) and in `process_exit`.
- **Lock-order rule:** while holding `filesys_lock`, a thread must not (a) acquire any other lock that
  can be held while waiting for I/O, or (b) block on a semaphore or condition owned by another
  process. User memory touched under the lock must already be validated (ADR-001) and, under VM,
  pinned (ADR-003). This rule is what makes the later VM page-fault path deadlock-free.

### Write-protected executables
`load` keeps the executable open, calls `file_deny_write`, and stores it in `thread.exec_file`.
`process_exit` closes it, and `file_close` re-allows writes. The checks in `validate_segment` match
stock Pintos again (the address wrap-around check had been dropped).

## Alternatives considered
- **Keep the child's `struct thread` alive until reaped (zombie threads):** simpler bookkeeping, but each
  zombie pins a 4 KB kernel page, and `multi-oom` spawns hundreds of children. A 40-byte record is cheaper.
- **A global lock for all records:** correct, but serialises unrelated processes. Per-record refcounts
  with a few instructions of interrupt-disable are enough.
- **Parent polls a flag instead of a load semaphore:** wastes CPU and can't block cleanly.

## Consequences
- `exec` now costs one extra context switch (the parent sleeps until the child has loaded). That's the
  same trade-off as POSIX `vfork`+`exec` error reporting.
- One global file-system lock limits file-system parallelism. That's acceptable for Pintos's
  non-thread-safe file system; finer locking would need a thread-safe buffer cache (out of scope).
