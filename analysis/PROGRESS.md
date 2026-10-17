# kernel-v2 progress

Build instructions: `OS_KERNEL_BUILD_INSTRUCTIONS.md` (milestones M0–M9) plus `OS_KERNEL_MASTER_PLAN.md`
and `analysis/IMPLEMENTATION_PLAN.md`. One branch (`kernel-v2`), one draft PR, updated each milestone.

| Milestone | Status | threads | userprog | vm | ipc | limits | Notes |
|---|---|---|---|---|---|---|---|
| M0 baseline + tooling | done | 26/27 | 64/80 | 70/113 | – | – | see below |
| M1 correctness | done | 27/27 | 76/80 | | | | 4 left are M2's pointer tests |
| M2 user-pointer validation | done | 27/27 | 80/80 | 89/113 | | | 3 known panics gone |
| M3 IPC (pipes, redirection) | done | 27/27 | 80/80 | 89/113 | 11/11 | – | pipes, exec_redirect, dup2, minish |
| M4 fuzzer, stress, stats, tracing, CI | done | 27/27 | 80/80 | 89/113 | 11/11 | – | 125k fuzzed calls, 0 panics; CI+CodeQL/cppcheck |
| M5 resource limits | done | 27/27 | 80/80 | 89/113 | 11/11 | 5/5 | fork-bomb, cpu-spin, fd-exhaust, neighbor contained |
| M6 virtual memory | – | | | | | | |
| M7 benchmarks | – | | | | | | |
| M8 browser demo | – | | | | | | |
| M9 presentation | – | | | | | | |

## M0 notes
- **Course code frozen:** the course submission is commit `4f7fc77`. The sandbox proxy refuses tag pushes,
  so it's preserved as branch `archive/course-submission-2025-11`. A local tag exists; Sumit can create the
  GitHub tag `course-submission-2025-11` from that branch (Releases → Draft new release → choose that
  commit), or run `git tag course-submission-2025-11 4f7fc77 && git push origin course-submission-2025-11`.
- **Build:** `Make.config` now passes `-fno-pie`/`-no-pie` itself, so Pintos builds with a stock modern
  GCC (CI runners, CodeQL) without the Docker gcc wrapper. The wrapper stays in the image for building
  older trees such as the course tag.
- **`scripts/test.sh [-l LABEL] SUITE...`** builds the image and runs suites; each suite writes
  `results.json` (per-test verdict and kernel ticks) plus summary and raw logs.
- **Baseline** (`analysis/test-results/m0-baseline/`): threads 26/27, userprog **64**/80, vm 70/113.
  Phase 1 measured userprog 63/80 three times; this run `syn-read` passed. The course code's
  `syn-read` result **depends on timing**: a real race (unlocked file-system access in `load`), not a
  deterministic bug. Counted as flaky; M1 fixes the cause.
- Bochs 2.6.11 for `--jitter` runs: attempted in M4 (stress testing), where it's used.

## M1 notes
- **Priority donation rewrite (plan §2.1):** `thread_recompute_priority` derives effective priority from
  live lock waiter lists; the cached `max_priority` fields and semaphore-level donation are gone.
  threads **27/27** (`priority-donate-multiple2` fixed). userprog 63/80 with the same failures as the
  M0 baseline, except `syn-read`, which failed in all three runs here (Phase 1 measured the same; it's
  the unlocked `load` race, fixed by §2.5). Results: `analysis/test-results/donation-rewrite/`.
- **Lifecycle redesign (ADR-002), §2.2, §2.4, §2.5** (this thread): shared reference-counted
  `child_record`, load handshake, exit status −1 by default, exit order, `filesys_lock` in `load`,
  write-protected executables, the restored `validate_segment` check and the stock fault message, plus a
  `cond_signal` fix (it no longer peeks at a possibly empty waiter list).
  **threads 27/27, userprog 76/80** (`analysis/test-results/m1/`). Now passing: `exec-missing`,
  `multi-oom`, `syn-read`, `syn-write`, `rox-*`, `bad-*`. The 4 failures left (`sc-bad-arg`,
  `sc-boundary-3`, `exec-bound-2/3`) are exactly the pointer-validation tests that M2 covers.
- PR #3 (donation rewrite, from a parallel thread) was reviewed and fast-forwarded into `kernel-v2`.

## M2 notes
- **ADR-001, `userprog/umem.[ch]`:** `validate_range` checks **every page** of a user range against the
  page directory (present, below `PHYS_BASE`, and writable when the kernel writes, via the new
  `pagedir_is_writable`); `validate_string` checks each page a string scan enters; `copy_in`,
  `copy_out` and `copy_in_string` validate first, then copy.
- `syscall.c` was rewritten around a dispatch table: the syscall number and **each 4-byte argument**
  are read with `copy_in`; paths (≤127 bytes) and command lines (≤1 page) are copied into kernel memory
  before use; read/write buffers are validated in full. Unknown numbers kill the caller.
- **Defence in depth:** a kernel-mode fault on a user address during a user process's system call ends
  the process (exit −1, `filesys_lock` released) instead of panicking.
- **Results** (`analysis/test-results/m2/`): userprog **80/80** (`sc-bad-arg`, `sc-boundary-3`,
  `exec-bound-2`, `exec-bound-3` fixed; the 3 known kernel panics are gone); threads 27/27; vm 89/113
  (VM itself is M6).

## M3 notes
- **ADR-005:** descriptors point to reference-counted open-file objects (`userprog/ofile.c`: file, pipe
  end, console); an array-based table hands out the lowest free descriptor (limit 1024). Pipes
  (`userprog/pipe.c`) are a 4 KB ring buffer with a lock and two condition variables.
- **New calls** (appended to `lib/syscall-nr.h`): `pipe`, `exec_redirect`, `dup2`. Every call is
  documented against POSIX in `docs/SYSCALLS.md` (claim C2).
- **Deviations, documented:** no `SIGPIPE` (write returns −1); closing a console descriptor is a no-op
  (the stock `close-stdout` test accepts only that or a kill). The test named `redirect-pipeline` in the
  instructions is `redir-pipeline`, because Pintos file names are limited to 14 characters.
- **`tests/ipc`** (new `src/ipc` project): 11/11. **`examples/minish`** runs pipelines, e.g.
  `minish echo one two three | upper | wc`, using two new filters, `upper` and `wc`.
- Results: `analysis/test-results/m3/`: ipc 11/11, userprog 80/80, threads 27/27, vm 89/113 (unchanged).

## M4 notes
- **Syscall fuzzer** (`src/examples/fuzz.c`, `scripts/fuzz.sh`): survivor children make only
  non-blocking hostile calls and run to completion (confirmed call count); bomb children pass bad
  pointers / unknown call numbers and must be killed by the kernel. Recorded run
  (`analysis/test-results/m4/fuzz-userprog.txt`): **125,000 confirmed calls over 25 seeds, 20/20 bombs
  killed, 0 leaked, 0 panics, 0 hangs, kernel healthy after.**
- **Sanity check vs the course tag:** `sc-boundary-3` **panics** the course kernel
  (`exception.c:91 in kill()`) and **passes** on kernel-v2 — the regression class the fuzzer guards.
- **Stress runner** (`scripts/stress.sh`): 20 runs each of ipc, userprog, threads, varying `-j`;
  results in `analysis/test-results/m4/stress.txt`.
- **Kernel statistics** (`threads/kstat.[ch]`): per-syscall and event counters, printed as `KSTAT` /
  `KSTAT-SYSCALLS` lines at power-off (after the checker's cut-off, so tests are unaffected).
- **Tracing** (`threads/trace.[ch]`, `-trace` option): a ring buffer of events dumped over serial;
  `scripts/trace2perfetto.py` converts it to Chrome trace JSON. Two samples in `traces/`.
- **CI** (`.github/workflows/ci.yml`): Docker build; threads/userprog/ipc in parallel with a
  regression gate against `analysis/expected-results.json` (`scripts/check_results.py`); a short fuzz;
  cppcheck and CodeQL (scoped to kernel code). cppcheck/CodeQL run on GitHub runners (not installable
  in this sandbox); the new modules already build under `-Werror`.

## M5 notes
- **ADR-004, `userprog/rlimit.[ch]`:** four limits. `LIMIT_PROCS` and `LIMIT_PAGES` are accounted across
  a process subtree via a shared reference-counted `struct rgroup` (so a fork bomb can't escape by
  spawning more); `LIMIT_FDS` and `LIMIT_CPU` are per-process, inherited by value. `setlimit` can only
  lower a limit; `getlimit` reads it; `uptime` returns timer ticks.
- **Enforcement:** `process_execute_redirect` admits a child to the group (fork-bomb cap); the timer
  interrupt charges the running user process's CPU budget and `intr_handler` kills it when spent (a pure
  spinner that makes no syscalls is still caught); `open`/`pipe`/`dup2` honour the fd cap; the user-page
  allocator charges the group (full mem-hog test arrives with VM in M6).
- **`tests/limits`** (new `src/limits` project): fork-bomb (capped at 7 of 8), fd-exhaust, cpu-spin
  (killed at its budget), getlimit (lower-only), neighbor (a good process finishes within an `uptime`
  deadline beside a fork bomb and a CPU spinner). 5/5. No regression: threads 27/27, userprog 80/80,
  ipc 11/11, vm 89/113, fuzzer still clean. Results: `analysis/test-results/m5/`.
