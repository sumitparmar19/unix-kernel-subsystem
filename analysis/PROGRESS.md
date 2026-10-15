# kernel-v2 progress

Build instructions: `OS_KERNEL_BUILD_INSTRUCTIONS.md` (milestones M0–M9) plus `OS_KERNEL_MASTER_PLAN.md`
and `analysis/IMPLEMENTATION_PLAN.md`. One branch (`kernel-v2`), one draft PR, updated each milestone.

| Milestone | Status | threads | userprog | vm | ipc | limits | Notes |
|---|---|---|---|---|---|---|---|
| M0 baseline + tooling | done | 26/27 | 64/80 | 70/113 | – | – | see below |
| M1 correctness | done | 27/27 | 76/80 | | | | 4 left are M2's pointer tests |
| M2 user-pointer validation | done | 27/27 | 80/80 | 89/113 | | | 3 known panics gone |
| M3 IPC (pipes, redirection) | – | | | | | | |
| M4 fuzzer, stress, stats, tracing, CI | – | | | | | | |
| M5 resource limits | – | | | | | | |
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
