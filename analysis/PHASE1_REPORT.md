# Phase 1 report: Unix-style OS kernel (Pintos)

**Status:** analysis complete for the source Sumit supplied. No kernel code was changed.
Last updated: 2026-10-15

## 1. Summary

- **Source:** `sumitkis@buffalo.edu_1_pa1_task2.zip`, a complete Pintos `src/` tree with file dates
  2025-11-22 to 2025-11-24. It has no `.git` folder, so there is no commit history. It is now imported
  under `src/`. The two `Sumit_parmar_ca_..._project_2_` zips are the same file (identical SHA-256): a
  **CSE 490/590 Computer Architecture gem5 cache study**, not part of this kernel. They are not imported.
- **Base:** Stanford `pintos-anon` at commit **`9f013d0`** (2021-04-15). It matches byte for byte except
  for Sumit's 11 files and 3 course-environment tweaks.
- **What Sumit built:** project 1 (alarm clock, priority scheduling + donation, MLFQS) and project 2
  (argument passing, all 13 system calls, exec/wait/exit, file descriptors, user-pointer checks):
  **1,197 lines added / 317 removed in 11 C files**. **No virtual memory, no file-system project,
  no assembly.**
- **Measured (Docker, QEMU):**

| Suite | Sumit | Stock Pintos (same base) | Sumit's own Nov-2025 run |
|---|---|---|---|
| threads (project 1) | **26 / 27** | 7 / 27 | – (no threads build in the zip) |
| userprog (project 2) | **63 / 80** | 0 / 80 | 63 / 80, same 17 failures |

## 2. Source, provenance and repository changes

| Item | Finding |
|---|---|
| Course | Folder naming (`sumitkis@buffalo.edu_1_pa1_task2`) matches the UB autograder. The handout's `utils/pintos` defaults to QEMU and `pintos-gdb` points at `/home/pintos`, both course tweaks. Course name and number unconfirmed (see Questions) |
| Base version | `pintos-anon` `9f013d0`, found by comparing against 60 upstream commits (`analysis/DIFF_SUMMARY.md`) |
| Authorship | No names, emails or partner credits anywhere in the 11 changed files. With no git history, authorship rests on Sumit's statement |
| Tests | `src/tests/` is **identical** to stock, so no test was edited to pass |
| Not imported | `userprog/build/` (build output), `misc/bochs-2.6.11.tar.gz` (5 MB third-party Bochs source), cscope/tags files |
| Permissions | The zip lost Unix modes; executables (`utils/pintos`, etc.) restored from the upstream commit's modes |
| Existing repo | Root `Make.config`/`Makefile*`/`LICENSE` are identical to `src/` (CRLF aside). They were Sumit's earlier partial upload. Left in place for now; removal proposed in Phase 2 |
| `.gitignore` bug | The old file was half UTF-16, and git read it as `*`, ignoring every new file. Rewritten in UTF-8 |

## 3. Build and tests

### How to run

```sh
docker build -t pintos .
docker run --rm -v "$PWD/analysis/test-results:/results" pintos userprog threads
```

Results land in `analysis/test-results/<project>/`: `summary.txt`, `verdicts.txt`, `check.log`,
`grade`, and each test's `.output`/`.result`/`.errors`. The Dockerfile (Ubuntu 22.04, gcc-multilib,
QEMU 6.2, Perl) changes no source. A `gcc` wrapper restores two old compiler defaults for 32-bit Pintos
builds: no PIE (a PIE kernel triple-faults at boot) and `-fcommon` (stock `filesys.h` defines a variable
in a header). Only `utils/setitimer-helper` is built, because the other two helpers include `<stropts.h>`,
which modern glibc removed. All suites run under QEMU, since current Bochs rejects the bochsrc that
`utils/pintos` writes.

### Results

| Suite | Passed | Runtime (`make -k check`) | Re-run serially (`-j1`) |
|---|---|---|---|
| userprog | **63 / 80** | 57 s at `-j4` | 63 / 80, identical failures (108 s) |
| threads  | **26 / 27** | 225 s at `-j4` | 26 / 27, identical failure (838 s) |

Course-era evidence: `analysis/test-results/course-run-2025-11-23/userprog-results`, written on the
course machine on 2025-11-23: 63/80, the same 17 failures. `syscall.c` changed once more after that
(2025-11-24), and the score is unchanged.

Caveat: the course ran the threads suite under Bochs (`threads/Make.vars`); here it ran under QEMU.
The one threads failure is deterministic logic (below), not timing.

### Why each test fails (from the test outputs and the code)

| Tests | # | Cause | Code |
|---|---|---|---|
| `bad-read`, `bad-read2`, `bad-write`, `bad-write2`, `bad-jump`, `bad-jump2` | 6 | **Behaviour is correct** (exit −1). Sumit shortened the message to `dying due to interrupt 0x0e`, dropping the `(%s)` exception name the checker uses to ignore fault messages, so the line counts as unexpected output | `userprog/exception.c:82` `kill` |
| `rox-simple`, `rox-child`, `rox-multichild` | 3 | Running executables are not write-protected: `file_deny_write` is never called | `userprog/process.c` `load`/`process_exit` |
| `sc-bad-arg` | 1 | Syscall **argument slots** `esp+1..3` are dereferenced unchecked; the exit status came out as −268370093, a word read from kernel memory | `userprog/syscall.c:112` etc. |
| `sc-boundary-3`, `exec-bound-2`, `exec-bound-3` | 3 | Only the **first byte** of the syscall number or a string is checked. Data that crosses into an unmapped page faults in kernel mode, and `kill` **panics the kernel** | `syscall.c:50` `check_user_ptr`, `exception.c` `kill` |
| `exec-missing` | 1 | `exec` returns the child's tid without waiting for the load result (returned 4, expected −1) | `process.c:89` `process_execute` |
| `syn-read`, `syn-write` | 2 | 9 of 10 concurrently exec'd children printed `load: … open failed` and exited with **0** (default `exit_status`), so `wait` returned 0 instead of the child's index. `load` calls `filesys_open`/`file_read` **without `filesys_lock`**; the most likely cause is unsynchronised file-system access during concurrent loads, compounded by the two exec issues above | `process.c:353`, `thread.c:634` |
| `multi-oom` | 1 | "crashed child should return −1": some children exited 0 (10 × `exit(0)` in the output). Consistent with failed loads exiting 0; not traced further | `process.c`, `thread.c:634` |
| `priority-donate-multiple2` | 1 (threads) | `lock_release` recomputes the remaining donation with `list_max(&locks_held, higher_prio, …)`, but `higher_prio` reads list elements as **threads** while `locks_held` holds **locks**. After a `printf` releases the console lock, `main` falls from 34 to 31 and thread `c` (32) runs early | `threads/synch.c:329` |

Other issues found by reading (no test covers them):
- `validate_segment` dropped the stock wrap-around check (`p_vaddr + p_memsz < p_vaddr`).
- `process_execute` links the child's `child_info` **after** `thread_create`. A child that runs and
  exits before the link would leave the parent's `wait` blocked forever (latent race, not observed).
- `check_user_buffer` checks only the first and last byte, not each page in between.
- `sema_down` donates to `sema->holder`, the last thread that downed a plain semaphore. Semaphores have
  no owner, so this can donate to an unrelated thread.
- `validate_ptr` and `do_exit` in `syscall.c` are unused.
- A parent that exits without waiting leaks its `child_info` records.

## 4. Design (what Sumit built, with references)

### 4.1 Threads (project 1): implemented
- **Alarm clock:** `devices/timer.c:107` `timer_sleep` inserts the thread into `sleep_list`, ordered
  by `wake_up_tick` (`wake_earlier`, `timer.c:38`), with interrupts off, then `thread_block`s.
  `timer_interrupt` (`timer.c:197`) pops and unblocks all due threads and yields on return if a woken
  thread outranks the current one. No busy waiting.
- **Priority scheduling:** `ready_list` is kept sorted with `higher_prio` (`thread.c:83`) in
  `thread_unblock` and `thread_yield`. `thread_create` yields if the new thread is higher;
  `thread_set_priority` (`thread.c:425`) yields if a ready thread now outranks the caller. Semaphore
  waiters are woken highest first (`sema_up`, `synch.c:132`); condition variables too
  (`sema_elem_higher_prio`, `cond_signal`).
- **Donation:** `lock_acquire` (`synch.c:227`) sets `waiting_on`, calls `donate_chain`
  (`thread.c:92`, follows `waiting_on → holder` up to 8 levels), and recomputes the holder's effective
  priority from `base_Priority` and each held lock's `max_priority`. `lock_release` (`synch.c:310`)
  restores base plus remaining donations (the buggy line above). Donation is disabled under MLFQS.
- **MLFQS:** `threads/fixed-point.h` (17.14). `timer_interrupt` increments `recent_cpu`, recomputes
  `load_avg` every second and flags recalculation. `thread_tick` (`thread.c:170`) recomputes all
  `recent_cpu` (per second) and priorities (every 4 ticks) and re-sorts the ready list. Formulas are in
  `mlfqs_update_priority`, `calc_recent_cpu` and `calc_load_avg` (`thread.c:467–510`). `nice` is
  inherited from the parent.

### 4.2 User programs (project 2): implemented
- **Argument passing:** `push_args` (`process.c:261`): strings right to left, word-align, `argv[argc]=NULL`,
  `argv[i]` pointers, `argv`, `argc`, fake return address. Up to 128 args; no stack-overflow check.
- **System-call path:** `int 0x30` → `syscall_init` registers `syscall_handler` at DPL 3 → reads the
  number at `esp` (checked) and the args at `esp+1..3` (unchecked) → `switch` dispatch → result in `f->eax`.
- **Calls implemented (13/13 project 2):** `halt`, `exit`, `exec`, `wait`, `create`, `remove`, `open`
  (fds from 2 upward, per process), `filesize`, `read` (fd 0 = keyboard via `input_getc`), `write`
  (fd 1 = `putbuf` in one call), `seek`, `tell`, `close`. Unknown numbers kill the process (−1).
- **User-pointer validation:** `check_user_ptr` (`syscall.c:50`): `is_user_vaddr` + `pagedir_get_page`.
  Applied to `esp`, the first byte of path/command strings, and the two ends of read/write buffers.
  Faults in user mode → `kill` sets exit −1; faults in kernel mode → panic.
- **Lifecycle:** `struct child_info` (`process.h`) is parent-owned: `exit_status`, `finished`, `waited`,
  `wait_sema`. `process_wait` (`process.c:171`) waits once per child, then frees the record; returns
  −1 for non-children and repeat waits. `process_exit` (`process.c:206`) publishes the status, `sema_up`s,
  prints `name: exit(n)`, closes all fds and destroys the page directory. **exec does not wait for
  load success** (no load semaphore).
- **Synchronisation:** one global `filesys_lock` around every file-system call in the syscall layer
  and in `process_exit`. Pointer checks happen before the lock is taken, so a killed process never holds
  it. Not taken in `load`. Per-child semaphore for wait/exit.
- **File descriptors:** per-thread `fd_list` of `{fd, file*}`, linear lookup (`find_fd`); all closed at
  exit. **No write-denial on running executables.**

### 4.3 Virtual memory (project 3) and file system (project 4): **not present**
`src/vm/` contains only the stock `Make.vars` and `Makefile`; `Makefile.build` still has
`# No virtual memory code yet.` No buffer cache, extensible files or subdirectories.

## 5. Resume claims: verdicts

| # | Claim | Verdict | Evidence | Suggested wording |
|---|---|---|---|---|
| 1 | "Implemented a POSIX-style system-call layer in C for the Pintos x86 kernel" | **True** (with the qualifier) | All 13 project 2 syscalls in `syscall.c:97`; 63/80 tests | "Implemented the system-call layer (13 Unix-style calls) for the Pintos x86 teaching kernel in C" |
| 2 | "process lifecycle (exec, wait, exit)" | **Partly true** | wait/exit solid (`wait-*`, `multi-recurse`, `multi-child-fd` pass); exec doesn't report load failure (`exec-missing`, `syn-*` fail) | "process creation and teardown (exec/wait/exit) with parent–child exit-status tracking" |
| 3 | "file I/O" | **True** | create/remove/open/close/read/write/seek/tell/filesize; all `create-*`, `open-*`, `read-*`, `write-*`, `close-*` pass; `rox-*` (write-protecting executables) fail | "per-process file descriptors and file I/O system calls" |
| 4 | "IPC" | **Not supported** as a term | No pipes, signals, shared memory or messages. The only parent–child channel is the exit status via `wait` | Drop "IPC". Use "parent–child synchronization (exec/wait)" |
| 5 | "inside a preemptive multitasking kernel" | **True as context, but** preemption itself is stock Pintos | Sumit **did** build project 1: priority scheduler, nested donation, MLFQS; 26/27 (stock 7/27) | Claim what he built: "Extended the kernel's preemptive scheduler with priority scheduling, nested priority donation and a 4.4BSD-style MLFQS (17.14 fixed point)" |
| 6 | "race-free semaphore synchronization" | **Not supported** | `syn-read`/`syn-write` fail; `load` reads the FS without `filesys_lock`; latent `child_info` linking race; donation bug | "semaphore- and lock-based synchronization (per-child wait semaphores, a global file-system lock)". Never "race-free" |
| 7 | "page-table validation of every user pointer" | **Partly true** | Mechanism is real (`is_user_vaddr` + `pagedir_get_page`, `syscall.c:50`); bad-pointer tests pass; but argument slots and page-crossing data aren't checked (`sc-bad-arg`, `sc-boundary-3`, `exec-bound-2/3` fail, two with a kernel panic) | "validated user-supplied pointers against the process page table before kernel access" (drop "every") |
| 8 | Title: "System Calls, Concurrency, and **Virtual Memory**" | **Not supported** | No `vm/` code; no frame table, page table, swap, stack growth or mmap | "Unix-Style OS Kernel: Scheduling, System Calls & Process Management in C (Pintos, x86)" |
| 9 | "C and **x86 Assembly**" | **Not supported** | No `.S` file changed; every `asm` in the changed files is stock | "C (x86, Pintos)". Fair to say he worked with the x86 interrupt frame, TSS and page tables |

## 6. One measured resume bullet

> Extended the Stanford Pintos x86 kernel in C (~1,100 lines): priority scheduling with nested
> priority donation and a 4.4BSD-style MLFQS scheduler, plus argument passing, 13 system calls and
> exec/wait/exit process management. Passes 26/27 scheduler and 63/80 user-program tests (stock: 7 and 0).

Every number is from §1/§3. "~1,100" is the whitespace-insensitive added-line count (1,131).

## 7. Three most important findings
1. **No virtual memory.** The resume title and "x86 Assembly" overclaim. Projects 1 and 2 are real,
   substantial, and they're what the title should say.
2. **The numbers are solid and reproducible:** 63/80 and 26/27, matching Sumit's own November 2025 run.
   Six of the 17 userprog failures come from one cosmetic message change; the behaviour is correct.
3. **The real gaps are in robustness and synchronisation:** exec/load handshake, the file-system lock
   in `load`, and per-byte pointer checks, so "race-free" and "every user pointer" must go. Explaining
   these gaps precisely is good interview material (`analysis/INTERVIEW_PREP.md`).

## 8. Questions for Sumit
1. **Course:** name and number (CSE 421/521?), semester (Fall 2025?), and the assignment names. The zip
   says `pa1_task2`. Was project 2 "PA1 task 2"?
2. **Partners:** individual or team? The UB 421 syllabus describes teams of three (unverified). If you had
   teammates, who are they and who wrote what? This decides the README credit.
3. **Is this the final version?** You mentioned several folders by test stage. This zip has both projects
   and matches your own 63/80 run. Do you have a later folder with more passing tests, or a threads-only
   one?
4. **Phase 2 direction:** freeze the course code as-is, or tag it and add your own post-course fixes
   (`PHASE2_PLAN.md` §0)?
5. **Repo layout:** OK to delete the duplicate root `Make.config`/`Makefile*` now that `src/` exists?
6. **Branch/PR:** this session can push only to `claude/serene-newton-57q2qk`. The PR is opened from there.
