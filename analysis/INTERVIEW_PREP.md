# Interview prep: the Pintos kernel

Everything here is grounded in Sumit's code in `src/` (file:line references are to that tree)
and in the measured test runs in `analysis/test-results/`. Where the code has a known gap, the
answer says so. In an interview, owning a gap is much stronger than being caught by it.

---

## 1. The project on one page (plain English)

**What Pintos is.** Pintos is a small teaching operating system from Stanford that runs on 32-bit
x86 inside an emulator (QEMU). The handout already boots, switches between threads on a timer, and
has a simple file system. What it can't do is the interesting part: put threads to sleep efficiently,
schedule by priority, or run user programs that make system calls. Students add those.

**What I built (about 1,100 lines of C across 11 files, on top of Stanford's `pintos-anon`).**

*Project 1: threads and scheduling*
- **Alarm clock.** The stock `timer_sleep` busy-waits: the thread keeps yielding until enough ticks
  pass. Mine puts the thread on a sleep list sorted by wake-up time and blocks it. The timer interrupt
  wakes every thread whose time has come. A sleeping thread costs zero CPU.
- **Priority scheduling.** The ready list is kept sorted by priority, so the scheduler always picks the
  highest-priority ready thread. A newly created or woken thread with higher priority preempts the
  running one.
- **Priority donation.** If a high-priority thread waits for a lock held by a low-priority thread, the
  holder temporarily borrows the higher priority so it can finish and release the lock. Without this, a
  medium-priority thread could starve both (priority inversion). Donation follows chains of locks up to
  8 levels deep.
- **MLFQS (multi-level feedback queue scheduler).** An alternative mode where the kernel computes
  priorities itself from each thread's `nice` value and recent CPU use, using 17.14 fixed-point
  arithmetic because the kernel has no floating point.

*Project 2: user programs*
- **Argument passing.** When a program starts, its command line is split into words and laid out on
  the new process's stack exactly as a C `main(argc, argv)` expects.
- **System calls.** All 13 project 2 calls: `halt`, `exit`, `exec`, `wait`, `create`, `remove`,
  `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`. A user program traps into the kernel with
  `int 0x30`, and my handler reads the call number and arguments from the user stack and dispatches.
- **Process lifecycle.** Each parent keeps a record per child (status, "finished" flag, a semaphore).
  `wait` blocks on that semaphore until the child exits, then collects the exit status, once.
- **File descriptors.** Each process has its own descriptor table; one global lock serialises all calls
  into the (non-thread-safe) file system.
- **User-pointer checks.** Before the kernel touches memory a user program passed in, it checks that
  the address is below `PHYS_BASE` and actually mapped in that process's page table, and kills the
  process (exit −1) if not.

**Results (measured in Docker, QEMU).** Threads suite: **26 of 27** (stock code passes 7).
User-program suite: **63 of 80** (stock code passes 0), the same 63 as my own run on the course
machine in November 2025.

**What doesn't work, and why.** (Know these cold.)
- A priority-donation corner case (`priority-donate-multiple2`): `lock_release` recomputes the
  remaining donation with a comparator written for threads, not locks.
- `exec` returns a pid even when the program fails to load, because the parent doesn't wait for a
  "load succeeded" signal. A failed load also exits with status 0 instead of −1.
- Running executables aren't write-protected (`file_deny_write` not used).
- Pointer validation checks the stack pointer and the first byte of strings and the ends of buffers,
  but not the argument slots themselves or every page a string/buffer spans. Two of those tests end in
  a kernel panic.
- `load` opens the executable without the file-system lock. With 10 children loading at once,
  9 failed to open their executable in the test run.
- Six `bad-*` tests do the right thing (exit −1) but fail on output format: I shortened the
  "dying due to interrupt" message and the checker no longer recognises it.

**No virtual memory (project 3) and no file-system project (project 4)** are in this tree.

---

## 2. Ten likely questions, with model answers

### Q1. Walk me through what happens when a user program calls `write(1, buf, 12)`.
1. The user-side stub in `lib/user/syscall.c` pushes the arguments and the call number
   `SYS_WRITE` onto the user stack and executes `int 0x30`.
2. The CPU switches to the kernel stack (from the TSS) and jumps through the interrupt table. Stock code
   saves all registers into a `struct intr_frame` and calls the handler I registered in
   `syscall_init` (`userprog/syscall.c:21`, DPL 3 so user code is allowed to trigger it).
3. `syscall_handler` (`syscall.c:97`) treats `f->esp` as an `int *`, checks it with
   `check_user_ptr` (below `PHYS_BASE` and mapped via `pagedir_get_page`), and reads the call number
   `*esp`.
4. For `SYS_WRITE` it reads `fd`, `buffer` and `size` from `esp+1..esp+3`, validates the buffer's
   first and last byte with `check_user_buffer`, and since `fd == 1` calls `putbuf` to write to the
   console in one piece (so output from different processes doesn't interleave mid-line).
5. The return value goes in `f->eax`. When the handler returns, `intr_exit` restores the user
   registers and `iret`s back to user mode, where `eax` becomes `write`'s return value.

*Honest follow-up:* the argument slots `esp+1..3` themselves aren't validated. A program that points
`esp` just below `PHYS_BASE` makes the kernel read argument words from kernel memory
(test `sc-bad-arg`: the process "exited" with status −268370093, a value from kernel memory). The fix is
to run `check_user_ptr` on every argument slot (all 4 bytes) before dereferencing it.

### Q2. How does `wait` work if the child exits first?
The exit status doesn't live in the child's `struct thread`, which is freed when the child dies. It
lives in a separate `struct child_info` (`userprog/process.h`) that the **parent** allocates in
`process_execute` and keeps on its `children` list. The child holds a pointer to it (`parent_info`).
When the child exits, `process_exit` (`process.c:206`) writes `exit_status`, sets `finished = true`,
and `sema_up`s the record's semaphore. If the parent calls `wait` later, `process_wait`
(`process.c:171`) finds the record, sees `finished`, skips the `sema_down`, reads the status and frees
the record. If the parent calls `wait` first, it blocks on `sema_down` until the child's `sema_up`.
The semaphore remembers the "up" either way, so the order doesn't matter.
A second `wait` on the same child returns −1, because the record was removed after the first one.
A pid that isn't the caller's child also returns −1 (not on the list).

### Q3. What happens if a user passes a pointer into kernel memory?
`check_user_ptr` (`syscall.c:50`) rejects it: `is_user_vaddr` fails for anything at or above
`PHYS_BASE` (0xC0000000), and `pagedir_get_page` returns NULL for unmapped user addresses. Either way
the process's `exit_status` becomes −1 and it calls `thread_exit`. The tests `create-bad-ptr`,
`open-bad-ptr`, `read-bad-ptr`, `write-bad-ptr` and `exec-bad-ptr` pass.
*Gap:* I check single bytes. A string or argument that **starts** valid but **crosses** into an unmapped
page slips through, and the kernel then page-faults in kernel mode. `kill` treats a kernel-mode fault as
a kernel bug and panics (tests `sc-boundary-3`, `exec-bound-2`, `exec-bound-3`). Two correct fixes: check
every page a string or buffer touches, or make the page-fault handler recognise a fault on a user address
during a system call and kill the process instead of panicking.

### Q4. How did you avoid deadlock between the file-system lock and process exit?
Two rules. First, every user pointer is validated **before** `filesys_lock` is taken (for example
`check_user_buffer` at the top of `SYS_READ`), so a process is never killed while holding the lock.
Second, `process_exit` takes `filesys_lock` itself only around each `file_close` and releases it each
time, and it never holds the lock while waiting on anything else. So there is no lock-order cycle.
*Gap to mention:* `load` (`process.c:330`) calls `filesys_open` and `file_read` **without** the lock.
That isn't a deadlock but a race: concurrent loads and file operations run inside a file system that
isn't thread-safe. That is the most likely cause of the `syn-read` / `syn-write` failures (9 of 10
children printed `load: ... open failed`).

### Q5. Explain priority donation and how you handle nested donation.
When thread H blocks on a lock held by L with lower priority, `lock_acquire` (`synch.c:227`) records
`waiting_on = lock` and calls `donate_chain` (`thread.c:92`). It walks
`lock → holder → holder's waiting_on → ...` up to 8 levels, raising each holder's priority to H's and
re-sorting it in the ready list if it's ready. Each lock remembers the highest priority waiting on it
(`max_priority`), and each thread keeps a `locks_held` list. On `lock_release`, the thread drops back to
its base priority, or to the highest `max_priority` among the locks it still holds. That way it keeps a
donation from a second lock (`priority-donate-multiple`). `thread_set_priority` only changes the base
priority and recomputes the effective one, so lowering your own priority doesn't cancel a donation
(`priority-donate-lower` passes).
*The one failing test:* in `lock_release` (`synch.c:329`) the "highest remaining donation" uses
`list_max(&cur->locks_held, higher_prio, …)`, but `higher_prio` reads its arguments as **threads**
while the list holds **locks**. It compares garbage, can pick the wrong lock, and the thread drops too far.
`thread_set_priority` does this loop correctly. Using the same loop in `lock_release` fixes it.

### Q6. Why fixed-point arithmetic for MLFQS, and how does it work?
The kernel doesn't save floating-point registers on a context switch, so floats aren't allowed in kernel
code. `threads/fixed-point.h` uses 17.14 fixed point: an int whose low 14 bits are the fraction
(`F = 1 << 14`). Add and subtract work directly; multiplying two fixed-point values needs a 64-bit
intermediate and a divide by `F`. Every second `calc_load_avg` (`thread.c:500`) computes
`load_avg = 59/60·load_avg + 1/60·ready_threads`. `recent_cpu` grows by 1 each tick for the running
thread and decays every second via `calc_recent_cpu`. Every 4 ticks, priority is
`PRI_MAX − recent_cpu/4 − 2·nice`, clamped (`mlfqs_update_priority`, `thread.c:467`). All 9 `mlfqs-*`
tests pass.

### Q7. How does the alarm clock avoid busy waiting, and why disable interrupts there?
`timer_sleep` (`devices/timer.c:107`) computes the wake tick, inserts the thread into `sleep_list`
ordered by wake time, and calls `thread_block`. The timer interrupt (`timer.c:197`) pops threads off
the front while their wake time has passed and unblocks them. Because the list is sorted, it stops at the
first one still sleeping, so the work per tick is small. Interrupts must be off while inserting:
`sleep_list` is shared with the interrupt handler, and an interrupt can't wait for a lock, so disabling
interrupts is the only way to make the insert-then-block atomic. It also prevents a lost wake-up between
the insert and `thread_block`.

### Q8. How does argument passing lay out the stack?
`push_args` (`process.c:261`) copies the words of the command line to the top of the new stack (right
to left), rounds `esp` down to a multiple of 4, pushes a NULL `argv[argc]` sentinel, then the addresses
of each string (so `argv[0]` ends up lowest), then `argv` itself, then `argc`, then a fake return
address of 0. `_start` sees exactly what a normal `main(argc, argv)` call would. All 5 `args-*`
tests pass, including double spaces (`strtok_r` skips empty tokens).
*Limits:* at most 128 arguments, and no check that the arguments fit in the single 4 KB stack page.

### Q9. What does your `exec` do if the program doesn't exist?
Honestly: it returns a valid pid anyway (test `exec-missing` fails; it printed `exec(...): 4`).
`process_execute` (`process.c:89`) creates the child thread and returns its tid immediately. Loading
happens later inside the child (`start_process`), and the parent never learns whether it worked. The
standard fix: add a `load_sema` and a `load_ok` flag to `child_info`. The parent `sema_down`s after
`thread_create`; the child sets `load_ok` and `sema_up`s right after `load()`; the parent returns −1 if
the load failed. While at it: a failed load should exit with −1. Today it exits with the default
status 0 that `init_thread` sets (`thread.c:634`).

### Q10. What would you do differently, or do next?
- **Synchronisation:** make `exec` wait for the load result, take `filesys_lock` inside `load`, and
  create the `child_info` **before** `thread_create`. Today the child is linked to its record after it
  starts, so a child that dies very fast could exit before the link exists, and its parent's `wait`
  would hang. That's a latent race no test triggered.
- **Robustness:** validate every byte range page by page, or handle kernel-mode page faults on user
  addresses by killing the process; call `file_deny_write` on the running executable (fixes the three
  `rox-*` tests).
- **Virtual memory (project 3):** supplemental page table, frame table with clock eviction, swap, stack
  growth and `mmap`. It's the natural next step and the reason the current title shouldn't say "virtual
  memory" yet.

---

## 3. Three hardest bugs (STAR stories)

There is no git history, so these come from the code and the test results, not a commit log.
Sumit should replace or extend them with what he actually remembers debugging.

### Bug 1: priority donation lost across multiple locks
- **Situation:** Threads A (34) and B (36) each waited on a different lock held by `main` (31). After
  `main` released one lock, it should have kept B's donation of 36.
- **Task:** Make the effective priority after a release equal to the highest donation still pending.
- **Action:** Tracked each lock's highest waiter (`max_priority`) and each thread's `locks_held`; on
  release, recompute from the base priority plus remaining locks. Nested chains are handled by
  `donate_chain` walking `waiting_on` links.
- **Result:** 26/27 threads tests pass, including `priority-donate-nest` and `-chain`. The remaining
  failure (`priority-donate-multiple2`) traces to one line: `list_max` with a thread comparator on a lock
  list. Knowing exactly why it fails is the interview story.

### Bug 2: `wait` when the child has already exited
- **Situation:** A parent might call `wait` long after the child died, after its `struct thread` was freed.
- **Task:** Keep the exit status alive and make `wait` work once, in either order.
- **Action:** Moved the status into a parent-owned `child_info` record with a semaphore. The child
  writes and signals; the parent waits, reads and frees.
- **Result:** `wait-simple`, `wait-twice`, `wait-killed`, `wait-bad-pid`, `multi-recurse` (15 levels
  of nested exec/wait) and `multi-child-fd` all pass.

### Bug 3: killing a process safely on bad pointers
- **Situation:** User programs pass NULL, kernel addresses and unmapped addresses to system calls on
  purpose (about 20 robustness tests).
- **Task:** Kill the offending process with exit −1 without crashing the kernel or leaking the file-system lock.
- **Action:** `check_user_ptr` checks `is_user_vaddr` and `pagedir_get_page` before any dereference,
  always **before** taking `filesys_lock`; `kill` sets exit status −1 on user faults.
- **Result:** All `*-bad-ptr`, `*-null`, `*-bad-fd` and `sc-bad-sp` tests pass. The remaining cases
  (strings or arguments crossing a page boundary) are the known gap in Q3.

## 4. Post-course changes (kernel-v2, written with AI pair-programming assistance)

### M1 · Priority donation rewrite (`priority-donate-multiple2` now passes; threads 27/27)
**What changed.** The course code cached donations in `lock->max_priority` (and `sema->max_priority`).
That cache only ever grew, and `lock_release` read it through a thread comparator on a list of locks
(Q5). The rewrite removes the cache. One function, `thread_recompute_priority (t)` (`thread.c`),
derives `t->priority` from live state:

    priority = max (base_Priority, priority of every thread in the waiter list of every lock t holds)

It runs whenever a donation can end or a base priority changes: in `lock_release` (after the lock
leaves `locks_held`), in `lock_acquire` once the lock is obtained (waiters still queued on it now
donate to the new holder), and in `thread_set_priority`. It re-sorts the thread in the ready list if
its priority changed.

**Donation itself** is still `donate_chain`: walk `waiting_on → holder → holder's waiting_on`, up to
8 levels, raising each holder to the donor's priority, stopping early once a holder is already at
least that high. A raise can't disagree with the recompute, because `lock_acquire` keeps interrupts
off from the donation until `sema_down` has put the donor on the lock's waiter list.

**Other fixes in the same change.**
- Semaphore-level donation removed from `sema_down`. A semaphore has no owner; `sema->holder` was just
  the last thread to call down, so donating to it was wrong (it affected `wait`'s semaphore).
- `lock_try_acquire` now adds the lock to `locks_held`; before, releasing a lock taken that way
  removed an element that was never in the list.
- `lock_release` keeps interrupts off through `sema_up`, so no thread can see `holder == NULL` while
  the semaphore is still taken (and skip donating).

**Trade-off to be ready for.** Recomputing walks every waiter of every held lock: O(locks × waiters)
per release instead of O(locks). In Pintos those lists are a handful of threads, and a cache that has
to be kept correct on every waiter change is exactly what caused the bug. Correct by construction
was worth more than the constant factor.

### M1 · Process lifecycle redesign (ADR-002) and file-system locking
**What changed** (`userprog/process.[ch]`):
- **Shared child record.** The parent allocates `struct child_record` *before* `thread_create` and hands
  it to the child with the command line. Parent and child each hold one reference (`refs = 2`). Each
  drops it exactly once (`child_record_release`), and the last one frees it. That replaced
  `thread_get_by_tid`, which looked the child up after it might already have exited.
- **Load handshake.** `exec` blocks on `load_sema` until the child reports `load_ok`. A missing program
  now returns −1 (`exec-missing`), and a failed load exits with −1, because `exit_status` now defaults
  to −1.
- **Exit order.** Print `name: exit(n)` → close files and the executable → orphan our own children (drop
  our references) → destroy the page directory → *then* publish the status and wake the parent. A
  parent that wakes from `wait` sees the child's memory already freed (`multi-oom` passes).
- **File-system lock in `load`.** `load` holds `filesys_lock` for every file access. Before, ten children
  loading at once raced inside the non-thread-safe file system, and some failed to open their own
  executable (`syn-read`/`syn-write` passed or failed depending on timing).
- **Write-protected executables.** `load` keeps the executable open with `file_deny_write`;
  `process_exit` closes it, which re-allows writes (`rox-*`).
- **`cond_signal`** orders waiters by the thread recorded in `cond_wait`, instead of peeking at a
  semaphore waiter list that can still be empty.

**Interview question:** *"Why a refcount instead of letting the parent free the record?"* Either side can
finish first. If the parent frees it, an orphaned child would write into freed memory when it exits; if
the child frees it, a parent that waits later reads freed memory. Two owners with a count is the
smallest design where neither order breaks.

### M2 · Page-table validation of every user pointer (ADR-001)
**What changed.** One module, `userprog/umem.c`, is the only code that touches user memory for a
system call. `validate_range (uaddr, size, write)` walks **every page** of the range and requires each
to be below `PHYS_BASE`, present in the process's page directory, and writable if the kernel will write
it. Strings are checked page by page while scanning for the NUL. The syscall dispatcher reads the
number and every argument word through `copy_in`; paths and command lines are copied into kernel
buffers before use; read/write buffers are validated in full before the copy.

**Why the old code failed.** It checked one byte. An argument slot sitting above `PHYS_BASE` was read
straight out of kernel memory (`sc-bad-arg` "exited" with a kernel word), and data crossing into an
unmapped page faulted *inside the kernel*, which panicked the whole machine (`sc-boundary-3`,
`exec-bound-2/3`).

**Interview question:** *"Why validate and then copy, rather than just copying and catching the
fault?"* Both are legitimate (Pintos documents the fault-based `get_user`). Validation keeps every
access decision in one auditable function and states the invariant explicitly; it costs one page-table
walk per page touched. The fault handler still keeps a safety net: a kernel fault on a user address
kills only the process.

### M3 · Real IPC: pipes, `exec_redirect`, `dup2` (ADR-005)
**What changed.**
- A descriptor now points to a reference-counted **open-file object** (`userprog/ofile.c`): a file, a pipe
  end, or the console. fd 0 and fd 1 are ordinary slots holding the console objects, so redirection just
  puts a different object there. `read`/`write` dispatch on the object type, not the descriptor number.
- **Pipe** (`userprog/pipe.c`): a 4096-byte ring buffer, one lock, and two condition variables
  (`readable`, `writable`). `read` blocks while empty and a writer exists, and returns 0 at EOF. `write`
  blocks while full and a reader exists, writes everything, and returns −1 if no reader exists. Closing an
  end broadcasts both conditions so blocked peers re-check.
- **`exec_redirect(cmd, in, out)`** is like `posix_spawn` with file actions: the child's fd 0/1 are the
  parent's objects (shared, refcounted). **`dup2`** follows POSIX.
- `minish` (`examples/minish.c`) runs `a | b | c`, closing its own pipe ends after each spawn so the next
  stage sees EOF.

**Proof.** `tests/ipc` 11/11: basic, EOF, a writer blocking on a full pipe, write with no reader,
1 MB checksummed through a 4 KB pipe, three writers sharing one pipe, a bad pointer (killed, kernel
fine), redirection, a three-process pipeline, `dup2` (shared offsets, stdout to a file and back), and
550 pipes created and destroyed without leaking kernel memory.

**Interview questions.**
- *"How does the reader know the writer is done?"* EOF means no write-end **objects** remain. A pipe
  counts objects, not descriptors: several descriptors (in several processes) can share one object, so
  EOF arrives only when the last one closes. That's why `minish` must close its own copy of each write
  end, or the last stage would wait forever.
- *"Why condition variables in a `while` loop?"* Pintos condition variables are Mesa-style: a woken
  thread re-acquires the lock later, and the state may have changed in between (another reader took the
  data). The loop re-checks the condition.
- *"Can pipes deadlock with the file system?"* No, by lock order: a thread holding a pipe lock never takes
  `filesys_lock`, and the reverse never happens either. User buffers are validated **before** the pipe
  lock is taken.
- *"What's the one deliberate deviation from POSIX?"* No `SIGPIPE` (Pintos has no signals), so writing to
  a pipe with no reader just returns −1. Also, closing the console descriptors is a no-op, to keep the
  stock Pintos contract.

### M4 · Fuzzing, stress, observability and CI
**Fuzzer.** A user program (`examples/fuzz.c`) that makes random system calls with hostile arguments
(bad pointers, pointers straddling `PHYS_BASE`, junk descriptors, huge sizes, unknown call numbers).
Children split in two: *survivors* make only non-blocking valid-but-hostile calls and run to completion
(so their call count is real — 125,000 over 25 seeds), and *bombs* pass bad pointers and must be killed
by the kernel. A kernel panic or a hang fails the run; a killed bomb is the kernel working.

**Why both kinds?** A child that passes a bad pointer is killed on that call, so it can't also prove
"thousands of calls ran". Separating the two gives an honest large call count *and* exercises the kill
path. Verified the fuzzer's class catches real regressions: `sc-boundary-3` panics the course
submission but passes on kernel-v2.

**Observability.** Counters (`kstat`) and a `-trace` ring buffer dumped at power-off, after the test
checker's cut-off line so tests are unaffected. `scripts/trace2perfetto.py` turns a dump into a
Perfetto timeline; the committed `priority-donation.json` shows donation unblocking a chain.

**CI** gates every push: it fails if any suite's pass count drops below the recorded baseline
(`expected-results.json`), runs a short fuzz, and runs cppcheck and CodeQL on the kernel sources.

**Interview question:** *"How do you know your fuzzer actually tests anything, rather than dying
immediately?"* The survivor children are the answer: each completes a fixed number of calls and exits
with a recognisable code, so the parent counts only calls that genuinely ran. 125,000 of them, zero
panics. And the same harness panics the old kernel on `sc-boundary-3`, which shows the checks are what's
holding the line.

### M5 · Resource limits, "cgroups-lite" (ADR-004)
**What changed.** Four limits (`userprog/rlimit.c`): live processes and user pages are capped across a
process **subtree** (a shared, reference-counted `struct rgroup`), while open descriptors and CPU time
are **per process** and inherited by value. `setlimit` can only lower a limit; `getlimit` reads it; a
new `uptime` returns timer ticks. Enforcement sits at the natural choke points: `process_execute`
(fork bomb), the timer interrupt + `intr_handler` kill (CPU spinner), `open`/`pipe`/`dup2` (descriptor
leak), and the user-page allocator (memory hog).

**Why subtree scope for processes?** *This is the key design question.* A per-process child limit cannot
stop a fork bomb: each new child would get its own fresh allowance and spawn N more, so the tree still
explodes. Process and memory caps must be accounted over the whole subtree, exactly as Linux cgroup v2
does. Descriptors and CPU are naturally per-process, like `RLIMIT_NOFILE` and `RLIMIT_CPU`.

**How is a pure CPU spinner killed if it makes no system calls?** The only kernel entry left is the
timer interrupt. It decrements the running user process's budget; when the budget hits zero it flags the
process, and `intr_handler` — which runs just after the external interrupt with `in_external_intr`
already cleared, so it is back in process context — calls `thread_exit()`. That's the one safe place to
terminate from the timer path.

**Proof.** `tests/limits` 5/5, including `neighbor`: a well-behaved process started next to a
self-limited fork bomb and a self-limited CPU spinner still finishes, and within a recorded `uptime`
deadline. No regression across threads/userprog/ipc/vm, fuzzer still clean.

**Interview question:** *"A tenant sets its own limit — can it then raise it to escape?"* No: `setlimit`
refuses any value above the current cap, like lowering a hard `rlimit`. And a child can never exceed what
its parent allowed, because limits are inherited (per-process by value, subtree by shared group) and only
ever tightened.

### M6 · Demand-paged virtual memory (ADR-003)
**What changed.** Four modules: a per-process **supplemental page table** (what each user page holds and
how to fetch it), a global **frame table** with **clock** eviction, **swap** over the swap disk, and
**mmap**. Programs now load pages lazily on fault, the stack grows on demand, and cold pages are evicted
to swap or written back to their file, so a working set larger than RAM runs fine. vm 113/113.

**The bug worth telling.** `page-linear` failed with `byte 0 != 0x5a` after heavy swapping. An
anonymous page written by the program, swapped out, then swapped back in, is clean again (fresh mapping)
but its only copy of the data is now in RAM. My first eviction logic treated a clean anonymous page as
"re-zero on next fault" and dropped it — losing the data. The fix is a sticky `dirtied` flag: once a
page has ever held written data, it is always preserved in swap. Knowing exactly why that corrupted is
a good systems story.

**The classic deadlock.** `read` holds the file-system lock while filling the user's buffer; if a buffer
page isn't resident, the fault handler needs the file system too → deadlock. The fix is pinning:
`read`/`write` fault in and pin every buffer page before taking the lock, then unpin after. Lock order
is `frame_lock` → `filesys_lock`/`swap_lock`, never the reverse.

**Interview questions.**
- *"How do you pick a victim?"* Clock / second chance: a hand sweeps frames; a frame whose accessed bit
  is set gets the bit cleared and a second chance; the first with a clear bit is evicted. It approximates
  LRU without per-access bookkeeping.
- *"What happens on a stack access below the stack?"* If the faulting address is within 32 bytes of the
  user `esp` (the `PUSHA` case) and inside the 8 MB limit, a fresh zero page is mapped; otherwise the
  process is killed. During a system call the kernel uses the `esp` it saved on entry, since the fault's
  `esp` is then a kernel address.
- *"Why is this relevant beyond a class project?"* It's the same mechanism as OS paging generally, and
  vLLM's PagedAttention applies exactly this idea — a page table, on-demand allocation, eviction — to a
  GPU's KV cache. Sandboxing untrusted code (the project's theme) needs both this and the limits from M5.
