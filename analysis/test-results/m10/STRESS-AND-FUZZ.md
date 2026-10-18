# v2.1 stress and fuzz record

## Regression (all suites, fixed kernel)
threads 27/27 · userprog 80/80 · ipc 11/11 · vm 113/113 · limits 6/6 ·
hardening 8/8 — see each `summary.txt` in this directory.

## Fuzz campaign (`fuzz-*.txt`)
The syscall fuzzer was extended to cover `mmap`, `munmap`, `setlimit`,
`getlimit`, `uptime`, `exec_redirect` and a multi-chunk big-buffer read, and
run on three kernels:

| kernel | calls | result |
|---|---:|---|
| userprog | **100,000** (20 seeds × 5,000) | PASS, 0 panics, 0 leaks |
| vm (incl. mmap/munmap) | 8,000 (4 × 2,000) | PASS, 0 panics, 0 leaks |
| limits | 8,000 (4 × 2,000) | PASS, 0 panics, 0 leaks |

This campaign is what found **B5** (`exec("")` panicking `dir_lookup`).

**Note on counts.** The VM-backed kernels (vm, limits) fuzz far slower under
QEMU — each `mmap` + big-buffer read drives the eviction path — so a
100,000-call run on them exceeds the host time budget (it was tried and timed
out). userprog carries the large-count run; vm and limits are validated at
8,000 calls each with all 21 calls (including `mmap`) exercised and zero
panics. CI fuzzes userprog and vm on every push at 5 seeds × 2,000.

## Stress (repeated clean runs)
The race-sensitive new suite was re-run to confirm the B3/B4 fixes are stable:

| suite | clean runs |
|---|---|
| hardening (incl. exit-evict 30 rounds, rw-pressure) | **5/5** (8/8 each) |

`exit-evict` and `rw-pressure` also repeat internally (30 exit-during-eviction
rounds; 8 workers × 60 read/write iterations under memory pressure), so each
single run is itself a stress test. A full 20×-every-suite sweep (threads is
~4 min/run) is impractical in this hosted environment's time windows; the
earlier M4 record has userprog 20/20 and ipc 20/20, and every suite is green
on each regular run.

## Not done here (honest follow-ups)
- **Neighbour slowdown (G3):** the `neighbor` test proves the sandbox property
  (a well-behaved process finishes beside a fork bomb and a CPU spinner) and
  passes with headroom under its 2,000-tick deadline. A precise
  alone-vs-with-abusers slowdown ratio over 10 runs each, to tighten that
  deadline from data, is a worthwhile follow-up but was not run here.
