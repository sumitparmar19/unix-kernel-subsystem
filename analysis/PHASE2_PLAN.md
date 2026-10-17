# Phase 2 plan (recommendation, not built)

> **Superseded by [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md)** (2026-10-15), which turns this into
> a phased build plan including virtual memory, fuzzing and benchmarks. Kept for the record.

Based on what Phase 1 measured: projects 1 and 2 are implemented (threads 26/27, user programs
63/80). There is **no virtual-memory or file-system project code**. The Docker harness already works.

## 0. Decisions Sumit makes first
1. **Fix the known bugs, or show them as they are?** Phase 1 changed no kernel code. Two honest options:
   - **A. Freeze the course code** (tag it `course-submission`) and show 63/80 and 26/27 with the failure
     analysis. Most honest; the failure write-up is itself a strong systems signal.
   - **B. Tag the course code, then fix the gaps on top as clearly labelled post-course commits.**
     The README would then show both numbers: "course submission: 63/80; after post-course fixes: N/80".
     Every fix in this direction must be written and understood by Sumit, since he has to defend it.
   Recommendation: **B, if Sumit writes the fixes himself** with review. The known fixes are small and
   well-defined (list below). Otherwise A.
2. **Keep "Virtual Memory" in the title?** Only by implementing project 3 (a multi-week project). Otherwise
   change the title now (see §4).

Known small fixes (for option B), each tied to failing tests:

| Fix | Where | Tests it should fix |
|---|---|---|
| Use a lock-aware loop (like `thread_set_priority`) in `lock_release` instead of `list_max(..., higher_prio)` | `threads/synch.c:329` | `priority-donate-multiple2` |
| Restore `(%s)` / `intr_name` in the "dying due to interrupt" message | `userprog/exception.c:82` | `bad-read`, `bad-write`, `bad-read2`, `bad-write2`, `bad-jump`, `bad-jump2` |
| Validate all 4 bytes of every syscall argument slot, and every page of strings and buffers | `userprog/syscall.c` | `sc-bad-arg`, `sc-boundary-3`, `exec-bound-2`, `exec-bound-3` |
| `exec` waits for the load result (load semaphore + flag in `child_info`); failed load exits −1; create `child_info` before `thread_create` | `userprog/process.c` | `exec-missing`, likely `multi-oom` |
| Hold `filesys_lock` around `filesys_open`/`file_read` in `load`; `file_deny_write` on the running executable, closed in `process_exit` | `userprog/process.c` | `rox-simple`, `rox-child`, `rox-multichild`, likely `syn-read`, `syn-write` |
| Restore the stock wrap-around check | `userprog/process.c:validate_segment` | none (security) |

## 1. README as a design document
- What this is: a Pintos kernel (Stanford `pintos-anon` @ `9f013d0`), projects 1 and 2, UB course.
  Credit Stanford and the course; name partners if there were any.
- **What I built vs what came with Pintos:** table taken from `analysis/DIFF_SUMMARY.md`.
- **Architecture diagram (Mermaid):** user program → `int 0x30` → `syscall_handler` → file system /
  process management; scheduler and timer underneath.
- **System-call flow diagram:** sequence diagram of `write()` from user stub to `iret`.
- **Process-lifecycle state diagram:** exec → running → exit → (zombie record in parent) → reaped by
  wait; plus the failure paths.
- **Synchronisation design:** sleep list (interrupts off), ready list ordering, donation chain,
  `child_info` semaphore, `filesys_lock`, and the known races, stated honestly.
- **Scheduler:** priority + donation, MLFQS formulas, fixed point.
- **Test results:** table with stock vs mine, link to the raw logs, the failing tests and why.
- **How to run:** the Docker commands.
- **What I learned.**
- Replace the UTF-16 one-line README (GitHub shows it, but tools mis-handle it).

## 2. Demo
An asciinema recording (or a GIF made from it) of `docker run ... pintos userprog` showing Pintos
booting in QEMU, running a few user programs (`echo`, `args-many`, `multi-recurse`) and the pass summary.
Kept under a minute, linked from the README.

## 3. CI
GitHub Actions workflow: build the `Dockerfile`, run `userprog` and `threads`, upload
`analysis/test-results/` as an artifact, and fail if the pass count drops below the recorded baseline
(63 and 26), not if any test fails. A README badge shows the pass counts. The `threads` suite takes
about 4 minutes, mostly in timing tests, so run it in a parallel job.

## 4. Resume fixes (from the Phase 1 verdict table)
- Title: **"Unix-Style OS Kernel: Scheduling, System Calls & Process Management in C (Pintos, x86)"**.
- Drop "virtual memory", "x86 Assembly", "IPC" and "race-free".
- One measured bullet (see `PHASE1_REPORT.md` §6).

## 5. Portfolio content
- A short case study, about 600 words: problem, design (donation and wait as the two highlights), results
  table, the failures and what they taught.
- Two diagrams reused from the README (system-call flow, lifecycle).
- One key number: **"63/80 user-program tests and 26/27 scheduler tests, from 0 and 7 on stock Pintos."**

## 6. Repo housekeeping
- Remove the duplicate top-level `Make.config` and `Makefile*` copies at the repo root (the real ones are
  now in `src/`), after Sumit confirms. Keep the root `LICENSE`.
- Keep `analysis/` (it's the evidence behind the numbers) or move it to `docs/`.
