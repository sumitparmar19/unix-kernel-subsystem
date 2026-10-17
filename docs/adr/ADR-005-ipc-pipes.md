# ADR-005: Inter-process communication: pipes, redirection and `dup2`

**Status:** accepted (M3) · **Code:** `src/userprog/ofile.[ch]`, `src/userprog/pipe.[ch]`,
`src/userprog/syscall.c`, `src/userprog/process.c` · **Claim:** C5, "IPC"

## Context
The course kernel's only channel between processes was the exit status that `wait` returns. Real IPC
needs a byte stream between running processes, and a way to connect a child's standard input and
output to it (`prog1 | prog2`).

Pintos `exec` is a combined fork and exec, and the stock tests require that children **don't** inherit
the parent's descriptors (`multi-child-fd`). So IPC is added through **new** calls only; stock `exec`
keeps its behaviour.

## Decision

### Open-file objects and the descriptor table
A descriptor no longer names a `struct file` directly. It points to a reference-counted
**open-file object** (`struct ofile`), as in Unix's open-file table:

```
struct ofile { enum ofile_type type; int refs; struct file *file; struct pipe *pipe; };
type ∈ { OF_FILE, OF_PIPE_READ, OF_PIPE_WRITE, OF_CONSOLE_IN, OF_CONSOLE_OUT }
```
- Each process has an array-based descriptor table (`struct fdtable`): O(1) lookup, the lowest free
  descriptor is handed out (POSIX), and the array grows on demand.
- `dup2` and `exec_redirect` make several descriptors (in one or more processes) share one `ofile`,
  including its file position, as POSIX does after `dup`/`fork`. Closing a descriptor drops one
  reference; the object is destroyed at zero.
- **fd 0 and fd 1 are ordinary entries** pointing to two static console objects. Redirection is
  therefore just a different object in slot 0 or 1; `read`/`write` never special-case descriptor
  numbers.

### Pipes
- A **one-page ring buffer** (4096 bytes) with a `lock` and two condition variables, `readable` and
  `writable`.
- The pipe counts its **read-end objects and write-end objects** (`readers`, `writers`), not
  descriptors. An end stays open while any descriptor in any process still refers to its object.
- `read`: blocks while the buffer is empty and a writer exists; returns **0 (EOF)** when it's empty and
  no writers remain; otherwise returns what's available, up to the request (a partial read, as in POSIX).
- `write`: blocks while the buffer is full and a reader exists, and keeps going until every byte is
  written (POSIX blocking semantics). Returns **−1 if no reader exists** at the start. POSIX would also
  raise `SIGPIPE`; Pintos has no signals, so only the error is returned. If the last reader goes away
  partway through, it returns the bytes written so far.
- Closing an end broadcasts on both conditions, so blocked peers re-check and see EOF or the error.
  The page and the object are freed when both counts reach 0. This happens automatically when a process
  exits, because `process_exit` closes its whole descriptor table.

### New system calls (appended to `lib/syscall-nr.h`)
| Call | Semantics |
|---|---|
| `bool pipe (int fds[2])` | creates a pipe; `fds[0]` reads, `fds[1]` writes. False if out of memory or descriptors |
| `pid_t exec_redirect (const char *cmd, int in_fd, int out_fd)` | like `exec`, but the child's fd 0 and fd 1 are the caller's `in_fd`/`out_fd` (−1 = console). The child holds its own references. Returns −1 for a bad descriptor or a failed load |
| `int dup2 (int oldfd, int newfd)` | POSIX: makes `newfd` refer to `oldfd`'s object, closing `newfd` first; returns `newfd`; `oldfd == newfd` returns `newfd`; −1 for a bad `oldfd` or out-of-range `newfd` |

### Synchronisation and lock order
- `pipe.lock` protects one pipe. A thread holding it **never takes `filesys_lock`**, and a thread holding
  `filesys_lock` never touches a pipe. So pipes and files can't deadlock with each other.
- User buffers are validated (ADR-001) **before** the pipe lock is taken; under VM they're also pinned
  (ADR-003). So the copy inside the lock can't fault.
- Waiting uses condition variables in loops (Mesa semantics), so spurious or stale wake-ups are harmless.

### Ownership across `exec_redirect`
The parent takes a reference on each object and hands both to the child in the exec arguments. If
`thread_create` fails, the parent drops them. Otherwise the child installs them as fd 0/1 **before**
loading, so a failed load releases them through the normal exit path. Every path drops each reference
exactly once.

## Alternatives considered
- **POSIX `fork` + inheritance:** would break the stock `multi-child-fd` contract and needs
  copy-on-write VM. Explicit `exec_redirect` is closer to `posix_spawn` with file actions, which real
  systems added for the same reason (spawn without copying an address space).
- **A larger or growable pipe buffer:** one page matches Linux's historical pipe size, and makes
  "writer blocks when full" easy to test (`pipe-full`).
- **Special-casing fd 0/1 inside `read`/`write`:** simpler at first, but redirection would then need a
  second code path. Console objects make redirection fall out naturally.

## Consequences
- Every descriptor operation is O(1). The table grows by doubling, bounded by the per-process descriptor
  limit (ADR-004).
- The fuzzer (M4) must avoid self-deadlock: a process that reads its own empty pipe while it still holds
  the write end blocks forever, which is the correct POSIX behaviour. The fuzzer keeps a model of the
  pipes it creates and only issues reads and writes that can't block on itself.
