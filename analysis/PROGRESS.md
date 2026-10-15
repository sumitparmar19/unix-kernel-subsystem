# kernel-v2 progress

Build instructions: `OS_KERNEL_BUILD_INSTRUCTIONS.md` (milestones M0–M9) plus `OS_KERNEL_MASTER_PLAN.md`
and `analysis/IMPLEMENTATION_PLAN.md`. One branch (`kernel-v2`), one draft PR, updated each milestone.

| Milestone | Status | threads | userprog | vm | ipc | limits | Notes |
|---|---|---|---|---|---|---|---|
| M0 baseline + tooling | done | 26/27 | 64/80 | 70/113 | – | – | see below |
| M1 correctness | – | | | | | | |
| M2 user-pointer validation | – | | | | | | |
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
