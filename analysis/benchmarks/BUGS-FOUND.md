# Two concurrency bugs the benchmark found (and how they were fixed)

Building the M7 benchmark meant running the VM under memory sizes and
policies the standard test suite never uses, with five parallel faulting
processes (`tests/vm/page-parallel`). That stress surfaced two real races in
the demand-paging code that a single pass of the suite had hidden. Both are
now fixed; this is the record.

## Symptom

`page-parallel` (a parent that `exec`s four children, each linearly scanning
a large anonymous array, then `wait`s for them) failed **nondeterministically**
— roughly 1 run in 10 at 4 MB RAM, and almost every run at 5 MB — with the
*parent* dying:

```
Page fault at 0: not present error reading page in user context.
page-parallel: dying due to interrupt 0x0e (#PF Page-Fault Exception).
  eip=0  cr2=00000000 error=00000004
```

`eip=0` is the tell: the parent returned into address 0. Its stack had been
corrupted — a return address overwritten — while it was blocked in `wait` and
its pages were paged out and back in. So the corruption was in the
swap/eviction path, triggered only by a particular timing of parallel faults.

## Bug 1 — `swap_in` freed the slot before reading it (the main cause)

`vm/swap.c` marked a swap slot free *before* reading its contents:

```c
lock_acquire (&swap_lock);
bitmap_reset (used_slots, slot);   // slot now free...
lock_release (&swap_lock);
for (...) block_read (slot, page); // ...but we read it only now
```

Between the `reset` and the `block_read`, a concurrent `swap_out` on another
process could `bitmap_scan_and_flip` that same slot, consider it free, and
`block_write` its own page into it. The swapping-in process then read a mix of
the two pages — including, fatally, a stack page's return address.

**Fix:** read first, free second, so the slot stays reserved until its data is
safely out.

```c
for (...) block_read (slot, page);   // read while still reserved
lock_acquire (&swap_lock);
bitmap_reset (used_slots, slot);     // free only after the read
lock_release (&swap_lock);
```

## Bug 2 — a page could be loaded mid-eviction

`frame.c`'s evictor drops `frame_lock` across the (slow) swap/file write so it
never holds the frame lock over I/O (the lock-ordering rule in ADR-003). But
that left a window: after `page_evict` cleared the victim's page-table entry
and set `kpage = NULL`, but *before* it recorded `has_swap = true`, the
victim's owner could fault on the page, find no swap and no frame, and
`load_frame` would **zero-fill it as a fresh anonymous page** — silently
dropping data still on its way to swap.

**Fix:** an "eviction in progress" barrier. The evictor marks the victim
`evicting` under `frame_lock` before releasing it; a faulting owner calls
`frame_await_eviction`, which waits on a condition variable until the evictor
has finished recording where the data went. Lock order is preserved (the
evictor still holds no lock during I/O), and the owner never observes a
half-updated entry.

## Result

After both fixes, the previously-flaky configurations pass every run:

| config | before | after |
|---|---|---|
| `page-parallel` clock, 4 MB | 9/10 | 6/6 |
| `page-parallel` clock, 5 MB | 0/5 | 5/5 |
| `page-parallel` random, 4 MB | 3/5 | 5/5 |

The full `vm` suite still passes 113/113 (`analysis/test-results/m7/`), and the
benchmark's paging curve is now clean at every memory size. Both fixes are
small and local; the value was in the diagnosis, which the benchmark's
parallel stress made possible.

*Code:* `src/vm/swap.c` (`swap_in`), `src/vm/frame.c`
(`frame_await_eviction`, `evict`), `src/vm/page.c` (`spt_load`),
`src/vm/page.h` (`evicting`). See ADR-003.
