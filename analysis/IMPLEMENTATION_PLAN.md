# Implementation plan: from course project to a complete, measured kernel

**For:** Sumit and the planning chat · **Executed by:** Claude Code, one phase per instruction file
**Based on:** the Phase 1 analysis of the code in `src/` (`PHASE1_REPORT.md`, `DIFF_SUMMARY.md`).
Every task below names the file and function it touches, the tests that prove it, and the number
that marks it done.

---

## 0. Read first

### 0.1 The real problem this project solves
An operating-system kernel exists to let **many untrusted programs share one machine safely and
efficiently**. That's the problem, and it's real: every server, phone and cloud VM depends on it.
This project should be able to prove three things about its kernel, with numbers:

| Property | Question it answers | How we prove it |
|---|---|---|
| **Isolation** | Can a buggy or hostile program crash the kernel or read kernel memory? | All robustness tests pass, plus a **system-call fuzzer** runs hundreds of thousands of random and adversarial calls with **zero kernel panics**. Today 3 known inputs panic the kernel and 1 reads kernel memory |
| **Correct concurrency** | Do processes, locks and the file system behave correctly under parallel load? | `syn-*`, `multi-oom` and the donation tests pass; the whole suite passes **N repeated stress runs with 0 flaky failures** |
| **Memory overcommit** | Can programs use more memory than the machine has? | **Virtual memory** (demand paging, swap, `mmap`) passes the VM suite; an **eviction-policy benchmark** compares page faults and swap traffic for different policies |

That turns "a course OS" into a systems project with a defensible design story: isolation, concurrency,
memory management, observability and measured trade-offs. It also makes the resume title
"System Calls, Concurrency, and Virtual Memory" **true**.

### 0.2 Where we start (measured 2026-10-15, Docker + QEMU)

| Suite | Today | Target after this plan |
|---|---|---|
| threads (`src/threads`) | 26 / 27 | **27 / 27** |
| userprog (`src/userprog`) | 63 / 80 | **80 / 80** |
| vm (`src/vm`) | 70 / 113 (7 of 34 VM tests pass only because they test error paths) | **113 / 113** |
| Kernel panics from user input | ≥ 3 known | **0** in fuzzing |

### 0.3 Non-negotiable rules for every phase
1. **Freeze the course work first.** Tag the current code `course-submission-2025-11` before any kernel
   change. The README always shows both: what Sumit submitted in Nov 2025, and what was added after.
2. **Authorship is visible.** Every commit Claude Code writes keeps its `Co-Authored-By` trailer.
   The README says post-course work was done "with AI pair-programming assistance". **Sumit must
   understand every change well enough to explain it in an interview.** Each phase updates
   `analysis/INTERVIEW_PREP.md` with the new design and its trade-offs, and Sumit reads that before the PR
   is merged. If he can't explain a change, it doesn't go on the resume.
3. **Never edit `src/tests/`.** Tests are the judge. A change that edits a test to pass is rejected.
4. **No copied solutions.** Don't take code from public Pintos solution repos; write it from the
   Pintos documentation and this codebase.
5. **Prove before merging.** Each PR includes: full suite results for every affected project, a
   comparison with the previous numbers (no regressions), and for concurrency changes a stress run
   (§5.2). Results are committed to `analysis/test-results/`.
6. **One phase per branch and PR.** Small, reviewable PRs. Phase N+1 starts after Phase N is merged.

### 0.4 Phase map

| Phase | Name | Output | Size |
|---|---|---|---|
| 2 | Correctness fixes for projects 1 and 2 | 27/27 threads, 80/80 userprog | M |
| 3 | Isolation and observability tooling | syscall fuzzer, stress runner, kernel statistics, CI | M |
| 4 | Virtual memory (Pintos project 3) | 113/113 VM suite | L (the big one) |
| 5 | Measured trade-offs | eviction-policy benchmark, report | S–M |
| 6 | Presentation | README design doc, diagrams, demo, resume | S |
| 7 (optional) | File-system project 4 | buffer cache with measured hit rate, extensible files, directories | L |

---

## Phase 2: Correctness fixes for projects 1 and 2

Goal: every known bug from Phase 1 fixed, with the right design rather than a patch.
**Done when:** threads 27/27 and userprog 80/80, both stable across 10 repeated runs (§5.2).

### 2.1 Priority donation: fix `lock_release` and lock bookkeeping
- **Bug:** `threads/synch.c:329` uses `list_max(&cur->locks_held, higher_prio, NULL)`, but
  `higher_prio` (`thread.c:83`) treats elements as `struct thread`, while `locks_held` holds
  `struct lock` (`holder_elem`). It compares garbage.
- **Fix:** one helper, `thread_recompute_priority(struct thread *t)`, which sets
  `t->priority = max(t->base_Priority, max over locks_held of lock->max_priority)`. Use it in
  `lock_release`, `thread_set_priority` (which already loops correctly; reuse it there) and
  `lock_acquire`.
- **Also fix `lock->max_priority` going stale:** today it only ever grows. Recompute it from the
  lock's current waiters (`lock->semaphore.waiters`) when a waiter leaves (in `lock_acquire` after
  `sema_down` returns) and on release.
- **Remove semaphore-level donation** from `sema_down` (`synch.c:63`): plain semaphores have no
  owner, so `sema->holder` is just the last thread that called down, and donating to it is wrong (it
  affects `child_info.wait_sema`). Donation belongs to locks only. Drop the `holder`/`max_priority`
  fields from `struct semaphore`.
- **Proves it:** `priority-donate-multiple2` passes; all other `priority-*` tests still pass.

### 2.2 User-fault message: restore the stock format
- `userprog/exception.c:82` `kill`: print `"%s: dying due to interrupt %#04x (%s).\n"` with
  `intr_name (f->vec_no)` again, exactly as stock. The checker recognises this line as a fault message.
- **Proves it:** `bad-read`, `bad-read2`, `bad-write`, `bad-write2`, `bad-jump`, `bad-jump2` (6 tests).

### 2.3 Safe user-memory access layer (design for VM now)
Today: `check_user_ptr` (`syscall.c:50`) checks one byte with `is_user_vaddr` + `pagedir_get_page`;
argument slots are not checked at all; strings and buffers are checked only at their ends.

**Design decision (write it up as ADR-001 in `docs/adr/`):** choose one approach and use it
everywhere.
- **Option A, check then access:** validate every page a range touches before touching it.
  Simple, but under VM `pagedir_get_page == NULL` no longer means "invalid" (the page may be lazily
  loaded or swapped out), so Phase 4 must replace the check with a supplemental-page-table lookup and
  pinning.
- **Option B, fault-tolerant copy (recommended):** add `get_user`/`put_user` byte accessors (the
  technique described in the Pintos documentation, §3.1.5). The page-fault handler, seeing a fault in
  kernel mode at a user address during a system call, sets `eax = -1` and resumes at the address saved
  in `eip`. Then build:
  - `copy_in (void *kdst, const void *usrc, size_t n)` → false on fault or address ≥ `PHYS_BASE`
  - `copy_in_string (char *kdst, const char *usrc, size_t max)` → copies a user string into a kernel
    page and fails on fault or overlong input
  - `copy_out (void *udst, const void *ksrc, size_t n)`

  This works unchanged when VM arrives: a fault on a valid lazy page is handled by the VM fault
  path, and only truly invalid addresses fail.
- **Apply it:** a `syscall_arg (f, i)` helper copies the i-th 4-byte argument with `copy_in`
  (fixes reading `esp+1..3` unchecked). The syscall number itself uses `copy_in` (fixes
  `sc-boundary-3`). Path names and the `exec` command line go through `copy_in_string` into kernel
  memory before use (fixes `exec-bound-2/3`; also removes the time-of-check/time-of-use gap). For
  `read`/`write` buffers, Phase 2 copies through a kernel bounce buffer in chunks of up to one page.
- Any failure → `exit(-1)`. **The kernel must never panic because of user input.** In `kill`, a
  kernel-mode fault outside a user-access window stays a panic, since that one really is a kernel bug.
- Delete the dead `validate_ptr` and `do_exit` and the old `check_user_ptr`/`check_user_buffer`.
- **Proves it:** `sc-bad-arg`, `sc-boundary-3`, `exec-bound-2`, `exec-bound-3`, and all existing
  `*-bad-ptr`, `*-boundary` and `*-null` tests still pass.

### 2.4 Process lifecycle: exec/load handshake, ownership, cleanup
Problems today, all in `userprog/process.c`:
1. `process_execute` returns the tid before the child has loaded, so `exec` of a missing program
   returns a pid (`exec-missing`).
2. A failed load exits with status 0, the default in `init_thread` (`thread.c:634`).
3. `child_info` is created **after** `thread_create` and linked via `thread_get_by_tid`. A child that
   exits first is never linked, and the parent's `wait` would block forever.
4. If the parent exits first, its `child_info` records leak.

**Redesign (ADR-002): a shared, reference-counted child record.**
- `struct child_info` gains: `struct semaphore load_sema`, `bool load_ok`, `int refcnt` (starts at 2:
  parent + child), and a small lock (or interrupt-disable) protecting `refcnt`/`exit_status`.
- `process_execute`: allocate `child_info` **first**; pass `{fn_copy, child_info*}` to the child
  through the `aux` pointer; after `thread_create` do `sema_down (&ci->load_sema)`; if
  `!ci->load_ok`, drop the parent's reference and return `TID_ERROR` (−1).
- `start_process`: after `load`, set `load_ok`, `sema_up (&ci->load_sema)`; on failure set exit status −1
  and exit.
- `process_exit`: write status, print the exit line **before** `sema_up` (so the line always precedes
  the parent's next output), release the child's reference; then release the parent's references on
  all of its own children and free those that reach 0. Whoever drops the last reference frees the record.
- Remove `thread_get_by_tid` (it walks `all_list` without protection).
- **Proves it:** `exec-missing`, `multi-oom` (it checks that resources are not leaked after
  hundreds of crashing children), and still `wait-*`, `multi-recurse`, `exec-*`.

### 2.5 File-system concurrency and executable protection
- `load` (`process.c:330`) calls `filesys_open`/`file_read` **without** `filesys_lock`. Hold the
  lock for those calls. The base Pintos file system is not thread-safe, and this is the likely cause
  of `syn-read`/`syn-write` (9 of 10 concurrent children failed to open their executable).
- **Write-protect running executables:** in `load`, keep the executable open, call
  `file_deny_write`, and store it in `struct thread` (`exec_file`); `process_exit` closes it
  (`file_close` re-allows writes). Fixes `rox-simple`, `rox-child`, `rox-multichild`.
- **Lock-ordering rule (write it into ADR-002):** never block on another lock or semaphore while holding
  `filesys_lock`, and never touch user memory while holding it (that's why 2.3 copies through kernel
  buffers first). This rule becomes essential in Phase 4.
- Restore the stock wrap-around check in `validate_segment` (`process.c:455`).
- **Proves it:** `syn-read`, `syn-write`, `syn-remove`, `rox-*`.

### 2.6 Phase 2 deliverables
- Code changes above; ADR-001 and ADR-002 in `docs/adr/`.
- `analysis/test-results/phase2/`: threads, userprog, and the 10-run stress results.
- Update `INTERVIEW_PREP.md`: the Q&As that currently describe gaps must describe the new design.

---

## Phase 3: Isolation and observability tooling
Goal: prove the isolation claim with a tool rather than a promise, and give later phases numbers to measure.

### 3.1 System-call fuzzer (user program)
- New test program, e.g. `src/examples/fuzz.c` (the `examples/` folder builds user programs with
  the user library). It takes a seed and an iteration count.
- **Design:** the parent `exec`s itself in "child" mode in a loop. Each child makes K random system
  calls with adversarial arguments: random and invalid syscall numbers; NULL; addresses at and around
  `PHYS_BASE`; pointers whose 4 bytes **straddle a page boundary**; strings that run into an unmapped
  page; huge sizes; negative and closed fds; `exec` of junk; `wait` on random pids. A child that gets
  killed is the expected outcome. The parent `wait`s and counts outcomes.
- **Pass criterion:** the kernel never panics or hangs; every child ends with a valid exit status
  (−1 for killed); the parent can still create processes and files afterwards (no leaks).
- Run from `scripts/fuzz.sh SEEDS ITERATIONS` in Docker; save a summary (seeds, calls made, outcomes).
- **Sanity check:** run it against the `course-submission` tag first. It should find the known panics,
  which proves the fuzzer works. Record that result.

### 3.2 Stress runner
- `scripts/stress.sh PROJECT RUNS`: runs `make check` RUNS times with varying `-j` and records every test
  that ever failed. Use it to show zero flakiness. Use QEMU, plus Bochs with `--jitter` if a compatible
  Bochs is added to the image (it randomises interrupt timing, which is good at exposing races).

### 3.3 Kernel statistics (observability)
- Counters in a small `struct kstats`: system calls per number, context switches, page faults
  (already counted in `exception.c`), and, from Phase 4, evictions, swap-ins, swap-outs and dirty
  write-backs. Print them at power-off next to the existing shutdown statistics
  (`devices/shutdown.c: print_stats`). The test checker ignores those lines, but **verify that all
  suites still pass after adding them**.
- These counters produce the Phase 5 numbers.

### 3.4 Continuous integration
- GitHub Actions: build the Docker image; run threads, userprog (and vm once Phase 4 starts) in
  parallel jobs; run a short fuzz (fixed seeds); upload `analysis/test-results` as an artifact; **fail
  on any regression against the recorded pass counts**. Add a README badge.

**Done when:** fuzzer finds 0 panics in ≥ 100,000 system calls across ≥ 10 seeds on the Phase 2
code; CI is green on `main`.

---

## Phase 4: Virtual memory (Pintos project 3)
Goal: programs can use more memory than the machine has. Build with `src/vm` (`Make.vars` already
defines `-DVM` and runs `tests/userprog`, `tests/vm`, `tests/filesys/base`).
**Done when:** `vm` suite **113/113**, userprog still 80/80, threads still 27/27, fuzzer still 0 panics.

Write ADR-003 (memory design) **before** coding; it's the core of the interview story.

### 4.1 New modules (add to `vm_SRC` in `src/Makefile.build`)
| File | Responsibility |
|---|---|
| `vm/page.[ch]` | **Supplemental page table (SPT)**, one per process: what *should* be at each user page and where its contents live |
| `vm/frame.[ch]` | **Frame table**, global: which user page owns each physical frame; allocation and eviction |
| `vm/swap.[ch]` | **Swap**: the `BLOCK_SWAP` device split into page-sized slots (8 sectors), with a bitmap |
| `vm/mmap.[ch]` (or inside `page.c`) | Memory-mapped files |

### 4.2 Supplemental page table
- Per-process `struct hash` (Pintos ships `lib/kernel/hash.c`) keyed by user page address, in
  `struct thread` under `#ifdef VM`.
- Entry: `upage`, `writable`, `kind` ∈ {FILE (executable segment), ZERO, SWAP, MMAP}, file source
  (`file`, `ofs`, `read_bytes`, `zero_bytes`), `swap_slot`, `kpage` (NULL when not resident), `pinned`.
- **Lazy loading:** `load_segment` (`process.c:488`) stops reading pages eagerly and only creates SPT
  entries. Pages are read on the first page fault.
- `process_exit` destroys the SPT: frees frames, frees swap slots, writes back dirty mmap pages.

### 4.3 Page-fault handler
`userprog/exception.c: page_fault` becomes the heart of VM:
1. If the fault came from a kernel user-access window (Phase 2.3 option B) and the address is invalid
   → return −1 to the copy routine. Otherwise, a kernel-mode fault is a real kernel bug → panic.
2. Look up the faulting page in the SPT. Not present and a valid entry exists → allocate a frame,
   fill it (read the file / zero it / swap in), install it with `pagedir_set_page`, return.
3. **Stack growth:** no entry, but the address is within 32 bytes below the user `esp` (the `PUSHA`
   case) and inside the stack limit (8 MB below `PHYS_BASE`) → add a ZERO page. For faults during a
   system call, use the user `esp` saved in `struct thread` on syscall entry, because `f->esp` is then a kernel address.
4. Write to a read-only page, or no valid entry → kill the process (exit −1).

### 4.4 Frame table and eviction
- Global list of frames `{kpage, owner thread, upage, pinned}` under `frame_lock`. User pages come
  only from `frame_alloc`, which wraps `palloc_get_page (PAL_USER)`.
- **Eviction policy: clock (second chance)** using `pagedir_is_accessed`/`pagedir_set_accessed`. Make
  the policy a compile-time switch (`clock`, `fifo`, `random`) for Phase 5.
- On eviction: clear the owner's mapping (`pagedir_clear_page`) **before** writing the page out, so the
  owner faults and waits instead of writing to a frame being reused. Then:
  dirty mmap page → write back to its file; dirty or anonymous page → swap out; clean file page → just
  drop it (it can be re-read).
- **Pinning:** frames being filled, being evicted, or in use by a system call are pinned and skipped by
  the clock hand.

### 4.5 Memory-mapped files
- `mmap (fd, addr)`/`munmap (mapid)` (syscall numbers already in `lib/syscall-nr.h`). Validate:
  fd not 0/1, `addr` page-aligned and non-zero, file length > 0, no overlap with any existing SPT entry
  (code, data, stack, other mappings). Use `file_reopen` so closing the fd doesn't end the mapping.
- `munmap` and process exit write dirty pages back to the file.

### 4.6 The classic deadlock, and how to avoid it (ADR-003 must cover this)
`read(fd, buf, n)` holds `filesys_lock`. If `buf` isn't resident, the page fault must read from a file
or swap, which may need `filesys_lock` → **deadlock**. Avoid it by rule:
- **Before** taking `filesys_lock`, fault in and **pin** every page of the user buffer. Do the file
  operation. Unpin. (With the Phase 2 bounce buffers this is simpler: the file system only ever touches
  kernel memory.)
- **Lock order:** `frame_lock` → `filesys_lock` / `swap_lock`; never acquire `frame_lock` while
  holding `filesys_lock`. Document the order in `docs/adr/` and in comments at each lock.

### 4.7 Phase 4 order of work (each step keeps the suites green)
1. Frame table wrapping `palloc` (no eviction yet) → all userprog tests still pass.
2. SPT + lazy loading of executables + page-fault path → userprog still passes; `page-linear` etc. start passing.
3. Stack growth → `pt-grow-*` tests.
4. Swap + clock eviction → `page-parallel`, `page-merge-*`, `page-shuffle` under memory pressure.
5. `mmap`/`munmap` → `mmap-*`.
6. Pinning and the deadlock rules → `page-merge-mm`, `parallel-merge`, stress runs, fuzzer.

---

## Phase 5: Measured trade-offs (the "system design" evidence)
Using the Phase 3 counters, compare **eviction policies** (clock vs FIFO vs random) and **memory
sizes** (`pintos --mem`, or the `-ul` user-page limit) on fixed workloads: `page-merge-seq`,
`page-merge-par`, `page-parallel`, `page-shuffle`, plus one custom workload with a working set of known
size. For each: page faults, evictions, swap-outs, dirty write-backs, ticks to finish. Repeat ≥ 5 times
and report the mean and spread.

Output: `analysis/BENCHMARKS.md` with tables and charts, and a short conclusion, e.g. "clock evicted N%
fewer hot pages than FIFO on the merge workload". Write it only from the measured data, whatever it shows.
If FIFO wins on some workload, say so and explain why.

---

## Phase 6: Presentation
- **README as a design document:** problem (§0.1), architecture diagram (Mermaid), system-call flow,
  process-lifecycle state diagram, memory-management diagram (SPT ↔ frame table ↔ swap), the ADRs,
  test results (course submission vs final), fuzzing and stress results, benchmarks, "what I learned",
  credits (Stanford Pintos; UB course; teammates if any; AI assistance for post-course work).
- **Demo:** asciinema/GIF from the Docker image: boot, run a program that uses more memory than the
  machine has, show swap counters, run the suite.
- **Resume (fill in only with measured numbers):**
  > Unix-Style OS Kernel: System Calls, Concurrency & Virtual Memory in C (Pintos, x86)
  > - Built demand-paged virtual memory (supplemental page table, clock eviction, swap, mmap, stack
  >   growth) so programs run with working sets larger than physical RAM; [N]/113 VM tests.
  > - Implemented 15 system calls with a fault-tolerant user-memory copy layer; a custom syscall fuzzer
  >   ran [N] adversarial calls with zero kernel panics.
  > - Priority scheduling with nested donation and MLFQS; [N]/27 scheduler tests; measured [X%] fewer
  >   swap-outs with clock vs FIFO eviction.

---

## Phase 7 (optional): File system (Pintos project 4)
Only if time allows after Phase 6. **Buffer cache** (64 sectors, clock replacement, write-behind,
read-ahead) with a measured hit rate and disk-read reduction; **extensible files** (indexed inode with
direct/indirect/doubly-indirect blocks); **subdirectories** (`chdir`, `mkdir`, `readdir`, `isdir`,
`inumber`); a fine-grained per-inode locking design to replace the global `filesys_lock`. Tests:
`tests/filesys/extended` (+ base, with VM enabled via `filesys/Make.vars`).

---

## Risks and how to handle them
| Risk | Mitigation |
|---|---|
| Sumit can't explain AI-written code in an interview | Rule 0.3-2: every phase updates `INTERVIEW_PREP.md`; Sumit reviews before merge; anything he can't explain stays off the resume |
| Timing-dependent tests flaky under QEMU | Stress runner (3.2); report flakiness honestly; consider adding Bochs 2.6.11 (patches are in `src/misc/`) for `--jitter` runs |
| VM is large and can stall | Phase 4 order of work (4.7) keeps every step green and mergeable; ship partial VM honestly if needed |
| Breaking the course record | `course-submission-2025-11` tag; README shows both sets of numbers |
| Academic integrity | No copying public solutions; course policy on publishing confirmed by Sumit |

## What the planning chat should send next
**Phase 2 instruction file:** sections 2.1–2.6 above, with the "done when" numbers (27/27, 80/80,
10 stress runs), the rules in 0.3, and a requirement to tag `course-submission-2025-11` first.
