# Kernel bugs found and fixed (M7 + v2.1 hardening)

Every bug below was found by the project's own tooling or by deliberately reading the concurrency and
resource-exhaustion paths the green test suites had not reached — the point of the measurement and
hardening work. Each has a regression test; where a bug produces a clean, repeatable failure on the
pre-fix kernel, the "before" is shown.

## Found by the M7 benchmark (parallel-fault stress on `tests/vm/page-parallel`)

Running the eviction benchmark under five parallel faulting processes produced a nondeterministic
parent crash at `eip=0` (a corrupted return address). Two races in the swap path:

1. **`swap_in` freed the slot before reading it** (`vm/swap.c`). A concurrent `swap_out` could reclaim
   the slot and overwrite it mid-read, mixing two pages — including a stack page. Fixed: read the page
   out, *then* free the slot.
2. **Load during eviction** (`vm/frame.c`, `vm/page.c`). Between `page_evict` clearing the victim's PTE
   and recording `has_swap`, the owner could fault and the loader would zero-fill a page whose data was
   still heading to swap. Fixed with an "evicting" barrier on a condition variable.

## Found by the v2.1 hardening review

Reading the new VM and limits code against the cases the suite never hit found four more bugs; the
extended fuzzer then found a fifth. Regression tests are in `src/tests/hardening/`.

### B1 — sandbox escape via `setlimit` (`vm`/limits, `rlimit.c`)
**Symptom.** A process capped in a subtree could call `setlimit` to its current cap and receive a
brand-new full-sized group, detached from its parent — so every descendant could repeat it and the
subtree grew without bound. The main sandbox claim was false.
**Fix.** Hierarchical resource groups: `setlimit` makes a *child* group, and admission/charging walk
from the leaf to the root, so every ancestor's cap binds (ADR-004).
**Before / after.** `tests/hardening/esc-procs` builds a 30-deep escaping chain. On v2.0 it is **not**
contained (the test fails, `exit(1)`); after the fix it is contained at the cap and passes. `esc-pages`
is the same for memory.

### B2 — user-triggerable kernel panic from unbounded pinning (`vm`, `syscall.c`/`frame.c`)
**Symptom.** `read`/`write` pinned the *whole* user buffer before I/O. A buffer larger than the
~367-page user pool pinned every frame, so `evict()` found no victim and ran
`PANIC("frame: no evictable frame found")` — reachable from a normal `read` into a big array.
**Fix.** Pin at most a 64 KB chunk at a time, I/O it, unpin, continue; and `evict()` returns NULL
instead of panicking, so an impossible allocation kills the offending process, not the kernel (ADR-003).
**Before / after.** `tests/hardening/pin-read` reads a 200-byte file through a 3 MB buffer.
On v2.0: `Kernel PANIC at ../../vm/frame.c:134 in evict(): frame: no evictable frame found`.
After the fix: `read` returns 200, contents intact, kernel healthy. `pin-write` and `pin-pipe` likewise.

### B3 — use-after-free on exit / `munmap` during eviction (`vm`, `page.c`/`frame.c`/`mmap.c`)
**Symptom.** The teardown paths freed a page's frame without waiting out an in-progress eviction, so
they could free a frame the evictor was still writing out and about to reuse — one physical page used
twice.
**Fix.** Teardown goes through `frame_release`, which waits while `p->evicting` under `frame_lock` and
only then frees; the evicted frame is marked `f->evicting` and `frame_free` asserts no one else frees
it. Rule: *only the evictor frees a frame it is evicting* (ADR-003).
**Status.** The window (during the evictor's swap I/O, lock dropped) is narrow; it was found by code
review, matches the "marginal-memory nondeterminism" noted in M7, and the fix is correct by
construction. `tests/hardening/exit-evict` runs 30 rounds of staggered exits under heavy paging and
passes; debug asserts in `frame_free`/`frame_release` guard the invariant. (The pre-fix panic is
probabilistic and was not reproduced in ~12 isolated runs, so this bug is documented as a
construction-level fix rather than with a captured panic.)

### B4 — check-then-pin TOCTOU in the load path (`vm`, `page.c`/`frame.c`)
**Symptom.** `spt_load` waited out eviction, dropped the lock, then separately checked "resident?" and
pinned. A preemption in the gap let another process evict the page; the pin then silently missed, and
the buffer was reported pinned-and-resident when it was not. The following file I/O faulted in the
kernel while holding `filesys_lock` — a non-recursive re-acquire, i.e. a panic. So "deadlock
eliminated by construction" was not yet true.
**Fix.** `frame_pin_resident` does wait-check-pin in a single `frame_lock` hold; a debug `ASSERT` in
the fault handler also fails fast if any kernel fault is taken while holding `filesys_lock` (ADR-003).
**Status.** The window is ~3 instructions; found by code review, fixed atomically.
`tests/hardening/rw-pressure` loops read/write on multi-page buffers under memory pressure and passes;
the assert guards against regression. (Pre-fix panic likewise probabilistic, not reproduced in ~6
isolated runs.)

### B5 — `dir_lookup` panic on `exec("")` (all userprog kernels, `process.c`) — found by the fuzzer
**Symptom.** The extended fuzzer (now covering `exec_redirect` and running on the vm/limits kernels)
hit `Kernel PANIC at ../../filesys/directory.c:125 in dir_lookup(): assertion 'name != NULL' failed`.
An empty or all-spaces command line has no program name; `strtok_r` returned NULL and `load` passed it
straight to `filesys_open`.
**Fix.** `load` fails cleanly when there is no program name, returning an error instead of calling the
file system with NULL.
**Before / after.** `tests/hardening/exec-empty` runs `exec("")`, `exec("   ")` and `exec_redirect("")`.
On v2.0 the first one panics the kernel; after the fix all three return `PID_ERROR` and the kernel
stays healthy. The fuzzer campaign (100k calls each on userprog, vm, limits) is now clean.

## Takeaway

A suite that is green on one pass hides timing bugs and resource-exhaustion corners. The three things
that found these — a measurement harness run under parallel load, a fuzzer extended to the newest
calls and kernels, and an adversarial re-reading of the locking — are exactly what a green checkmark
does not give you.
