# System calls: semantics and POSIX comparison

Every call made through `int $0x30` (`src/userprog/syscall.c`). Arguments are fetched and validated by
`userprog/umem.c` (ADR-001). **Any invalid user pointer, or an unknown call number, ends the caller with
exit status −1.** Pintos has no `errno`, so failures are reported only through the return value.

Legend for the POSIX column: **same** means POSIX return values and edge cases; **differs** names each
difference. Pintos programs are single-threaded, have no signals and use a flat file system with names of
at most 14 characters.

## Process management

| Call | Behaviour | POSIX counterpart | Comparison |
|---|---|---|---|
| `void halt (void)` | Powers the machine off. | `reboot(RB_POWER_OFF)` | **differs:** unprivileged (Pintos has no users) |
| `void exit (int status)` | Ends the caller; the kernel prints `name: exit(status)`. Its children are orphaned: their records stay valid until they exit (ADR-002). | `_exit` | **differs:** the full `int` is kept (POSIX passes only the low 8 bits to `wait`) |
| `pid_t exec (const char *cmd_line)` | Starts a new process running `cmd_line` (program name + arguments), with the console as fd 0/1. **Returns only after the child has loaded**: the pid, or −1 if the program can't be loaded. The caller keeps running. | `posix_spawn` | **same model as `posix_spawn`** (not `execve`, which replaces the caller). Children don't inherit descriptors (only fd 0/1, set explicitly) |
| `int wait (pid_t pid)` | Waits for child `pid` and returns its exit status (−1 if it was killed). Returns −1 at once if `pid` is not the caller's child or was already waited for. Works whether the child exits before or after the call. | `waitpid(pid, &st, 0)` | **differs:** returns the status directly; −1 instead of `ECHILD` |
| `pid_t exec_redirect (const char *cmd_line, int stdin_fd, int stdout_fd)` | Like `exec`, but the child's fd 0 and fd 1 are the caller's `stdin_fd` and `stdout_fd` (−1 = console). The child shares those open-file objects (and file offsets). −1 for a bad descriptor or a failed load. | `posix_spawn` with `posix_spawn_file_actions_adddup2` | **same idea**, limited to fd 0 and 1 |

## Files and descriptors

| Call | Behaviour | POSIX counterpart | Comparison |
|---|---|---|---|
| `bool create (const char *file, unsigned size)` | Creates `file` with a fixed `size`. False if it exists or the name is empty or too long (>14 bytes for the file system; >127 is rejected before the file system is asked). | `open(O_CREAT\|O_EXCL)` + `ftruncate` | **differs:** files can't grow later (base Pintos file system), so the size is set here |
| `bool remove (const char *file)` | Removes the name. Open descriptors keep working until closed. | `unlink` | **same** (including "open files survive unlink") |
| `int open (const char *file)` | Opens for reading and writing. Returns the **lowest free descriptor**, or −1. | `open(O_RDWR)` | **same** descriptor rule; −1 instead of `errno` |
| `int filesize (int fd)` | Length of the open file, or −1 if `fd` isn't an open file. | `fstat().st_size` | — |
| `int read (int fd, void *buf, unsigned size)` | Reads into `buf` (every page must be **writable**). File: up to `size` bytes, 0 at end of file. Pipe: blocks while empty and a writer exists; returns what's available; 0 at EOF. Console: blocks until `size` bytes arrive. −1 for a bad or write-only descriptor. | `read` | **same** for files and pipes. **Differs** for the console: Pintos reads exactly `size` bytes (no line discipline) |
| `int write (int fd, const void *buf, unsigned size)` | File: writes up to the end of the file (no growth) and returns the count. Pipe: blocks until all bytes are written; −1 if no reader exists. Console: writes all bytes in one piece. −1 for a bad or read-only descriptor. | `write` | **same** for pipes, except **no `SIGPIPE`** (Pintos has no signals; −1 is returned). Files can't grow |
| `void seek (int fd, unsigned position)` | Sets the file position (may be past the end). No effect on pipes or the console. | `lseek(SEEK_SET)` | **differs:** no return value |
| `unsigned tell (int fd)` | Current file position; −1 if not a file. | `lseek(fd, 0, SEEK_CUR)` | — |
| `void close (int fd)` | Closes `fd`; the file or pipe end goes away when its last descriptor (in any process) closes. Bad descriptors are ignored. **Closing fd 0/1 while it refers to the console is a no-op**; a redirected fd 0/1 (pipe or file) closes normally. | `close` | **differs:** no return value; console descriptors can't be closed (the stock Pintos contract that `close-stdin`/`close-stdout` test) |
| `bool pipe (int fds[2])` | Creates a pipe with a 4096-byte buffer; `fds[0]` reads, `fds[1]` writes (lowest free descriptors). False if out of memory or descriptors. | `pipe` | **same** |
| `int dup2 (int oldfd, int newfd)` | Makes `newfd` share `oldfd`'s open-file object (and offset), closing `newfd` first. Returns `newfd`; `oldfd == newfd` returns `newfd` without change. −1 if `oldfd` isn't open or `newfd` is out of range (0–1023). | `dup2` | **same** |

## Resource limits (docs/adr/ADR-004)

| Call | Behaviour | POSIX counterpart | Comparison |
|---|---|---|---|
| `bool setlimit (int resource, int value)` | Lowers a limit on the caller (`LIMIT_PROCS`, `LIMIT_PAGES` are shared with future descendants; `LIMIT_FDS`, `LIMIT_CPU` are per-process). Refuses to raise a limit (returns false). | `setrlimit` | **differs:** unprivileged processes can only lower, as with a hard limit; subtree scope for procs/pages is cgroup-like |
| `int getlimit (int resource)` | The caller's current cap (−1 = unlimited). | `getrlimit` | — |
| `int uptime (void)` | Timer ticks since boot (100/second). | `times` / `clock_gettime(CLOCK_MONOTONIC)` | coarse, tick-granular |

`resource` is 0 `LIMIT_PROCS`, 1 `LIMIT_PAGES`, 2 `LIMIT_FDS`, 3 `LIMIT_CPU`. Over a subtree process or
page cap, `exec` / allocation fails; over the CPU budget, the process is killed (exit −1); over the fd
cap, `open`/`pipe`/`dup2` return −1. Subtree caps are **hierarchical** (ADR-004): a descendant that
lowers its own cap makes a tighter child group inside its parent's, so it can never mint a fresh
allowance to escape an ancestor's limit.

## Memory mapping (VM builds only, docs/adr/ADR-003)

Compiled only with `-DVM`, so there are 19 system calls in a plain user-program kernel and **21** with
virtual memory.

| Call | Behaviour | POSIX counterpart | Comparison |
|---|---|---|---|
| `mapid_t mmap (int fd, void *addr)` | Maps the open file `fd` into the caller's address space at page-aligned `addr`, lazily; dirty pages are written back on eviction and at unmap. Fails (−1) on fd 0/1, a zero/misaligned `addr`, a zero-length file, or any overlap with existing pages. | `mmap(addr, len, PROT_READ\|PROT_WRITE, MAP_SHARED, fd, 0)` | **differs:** length comes from the file; address must be given (no kernel-chosen placement) |
| `void munmap (mapid_t mapping)` | Unmaps a region, writing back any dirty pages first. | `munmap` | **same** effect; identified by the id `mmap` returned |

## Limits

- Descriptors: 0–1023 per process (like `RLIMIT_NOFILE` = 1024). The resource-limits work makes this configurable per process.
- Path names: 127 bytes at the system-call layer; the Pintos file system itself allows 14.
- Command lines: one page (4096 bytes), including arguments.
