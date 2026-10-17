# Résumé evidence map

Every résumé claim for this project, with the code, the design record, and the test or measurement
that backs it. The rule for this repository: **nothing goes on the résumé that isn't true here and
that I can't explain** (`analysis/INTERVIEW_PREP.md`). Results are reproduced each milestone under
`analysis/test-results/`.

**Authorship.** Course projects 1–2 (C1, C5) are my university coursework, frozen at tag
`course-submission-2025-11`. The post-course work (C2–C4, C6–C9) was built as a portfolio project
with AI pair-programming assistance (Claude), disclosed per-commit; the design, diagnosis and review
are mine.

| # | Claim | Backed by (code) | Design record | Evidence |
|---|---|---|---|---|
| C1 | System-call layer (19 POSIX-style calls) in C for the Pintos x86 kernel | `src/userprog/syscall.c`, `userprog/umem.c` | [SYSCALLS.md](../docs/SYSCALLS.md) | userprog 80/80; fuzzer ~125k calls, 0 panics |
| C2 | Process lifecycle: `exec`/`wait`/`exit` with a load handshake and exit-status tracking | `src/userprog/process.c` | [ADR-002](../docs/adr/ADR-002-process-lifecycle.md) | `wait-*`, `multi-recurse`, `exec-missing`, `exec-multiple` pass |
| C3 | Per-process file descriptors and file I/O | `src/userprog/ofile.c`, `syscall.c` | ADR-002 | `open-*`, `read-*`, `write-*`, `close-*`, `rox-*` pass |
| C4 | Real IPC: pipes, `dup2`, `exec_redirect` (posix_spawn-style) | `src/userprog/pipe.c`, `ofile.c`, `process.c` | [ADR-005](../docs/adr/ADR-005-ipc-pipes.md) | ipc 11/11 (`pipe-*`, `dup2-*`, `redir-pipeline`) |
| C5 | Preemptive scheduler: priority scheduling, **nested** priority donation, 4.4BSD MLFQS (17.14 fixed point) | `src/threads/thread.c`, `synch.c`, `fixed-point.h` | PHASE1_REPORT §5 | threads 27/27 (stock 7/27); `priority-donate-*`, `mlfqs-*` pass |
| C6 | Semaphore-, lock- and condition-variable-based synchronization with a stated lock order | `threads/synch.c`, `userprog/process.c`, `vm/frame.c` | ADR-002, ADR-003 | donation chain, `filesys_lock`, eviction barrier; no deadlock in any suite |
| C7 | Page-table validation of every user pointer (faulted in, pinned, resident under VM) | `src/userprog/umem.c`, `userprog/exception.c` | [ADR-001](../docs/adr/ADR-001-user-memory-access.md) | `bad-*`, `sc-bad-*`, `sc-boundary-*`, `exec-bound-*` pass |
| C8 | Resource limits / sandboxing ("cgroups-lite"): per-subtree procs & pages, per-process fds & CPU budget | `src/userprog/rlimit.c`, `threads/{thread,interrupt}.c`, `devices/timer.c` | [ADR-004](../docs/adr/ADR-004-resource-limits.md) | limits 6/6 (`fork-bomb`, `cpu-spin`, `fd-exhaust`, `mem-hog`, `neighbor`) |
| C9 | Demand-paged virtual memory: supplemental page table, frame table with clock eviction, swap, mmap, lazy load, stack growth | `src/vm/{page,frame,swap,mmap}.c` | [ADR-003](../docs/adr/ADR-003-virtual-memory.md) | vm 113/113; measured policy comparison in `benchmarks/` |

## Résumé-ready bullets

Each is literally true above and defensible in `INTERVIEW_PREP.md`.

- **Extended the Stanford Pintos x86 kernel (C) into a multi-tenant sandbox** that runs untrusted
  programs with process/memory/CPU limits, real IPC, and demand-paged virtual memory — all suites
  green (threads 27/27, userprog 80/80, ipc 11/11, vm 113/113, limits 6/6) in CI with a regression
  gate, CodeQL and a 125k-call syscall fuzzer (0 kernel panics).
- **Designed demand-paged virtual memory** (supplemental page table, frame table, swap, mmap, lazy
  loading, stack growth) and **measured the eviction policy**: clock vs FIFO vs random over a
  policy × workload × memory × seed matrix. Clock cut swap-ins ~100× vs FIFO (3 vs ~320) on a
  parallel scan; FIFO thrashed a write-heavy workload past its time budget.
- **Found and fixed two concurrency races** the benchmark's parallel stress exposed (a
  free-before-read race in the swap path and a load-during-eviction window), diagnosed from an
  `eip=0` crash to a condition-variable barrier and a reordered swap read.
- **Built "cgroups-lite" resource limits**: per-subtree process/page accounting and per-process
  fd/CPU budgets, so a fork bomb is contained and a CPU hog is killed while a well-behaved process
  still meets its deadline (`tests/limits/neighbor`).
- **Hardened the system-call boundary**: every user pointer validated against the page table (and,
  under VM, faulted in and pinned for the call), eliminating a read-into-unloaded-buffer deadlock by
  construction.

## What I would (still) do next

- Close the single marginal-memory nondeterminism noted during M7 investigation (now fixed) by
  adding an eviction/fault event-ordering assertion in a debug build.
- A second-level page cache and read-ahead for swap, measured the same way.
- A real `fork`/`execve` split (today `exec` is `posix_spawn`-style), to run an unmodified shell.
