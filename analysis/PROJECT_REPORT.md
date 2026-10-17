# Project report & handoff — unix-kernel-subsystem

**Date:** 2026-10-17 · **State:** v2.0 build complete, **all merged to `main`**, CI green.
**Repo:** https://github.com/sumitparmar19/unix-kernel-subsystem (public)

This document is the single place a new person — or a new Claude session — can read to understand
what this project is, everything that was built, how it is organized, how to run it, and what comes
next. It is written to be self-contained; you should not need to re-read the whole git history.

---

## 1. What this project is (in one paragraph)

A Unix-style operating-system kernel built on **Stanford Pintos** (x86, C). It started as a
university course submission (Pintos projects 1–2: a scheduler and a system-call layer). It was then
extended, as a portfolio project, into a kernel that **runs many untrusted user programs safely on
one machine** — the sandbox / cloud-multitenancy theme: isolate programs from the kernel and each
other, bound what each may consume (processes, memory, file descriptors, CPU), give them real IPC
(pipes), and page a working set larger than physical RAM to disk. Every capability is backed by
tests; the page-replacement design is backed by measurements. Post-course work was done with AI
pair-programming assistance (Claude), disclosed per commit.

## 2. Current status at a glance

| Thing | State |
|---|---|
| Build | Milestones M0–M9 complete |
| Git | Everything merged to **`main`** (PR #1 then PR #2, merge commits, history preserved) |
| CI | **Green** on the merged commit (`7879bb0`): 5 suites + fuzz + cppcheck + CodeQL |
| Tests | threads **27/27**, userprog **80/80**, ipc **11/11**, vm **113/113**, limits **6/6** |
| Fuzzer | ~125k adversarial syscalls, **0 kernel panics** |
| Tag | `v2.0` created **locally** (token can't push tags — a one-click for the user) |
| Course freeze | tag `course-submission-2025-11` + branch `archive/course-submission-2025-11` |

**Two actions only the repo owner can do** (the automation's GitHub token can't):
1. **Enable GitHub Pages**: Settings → Pages → Deploy from branch, `/docs` — publishes the
   benchmark site + demo at the Pages URL.
2. **Cut the `v2.0` tag / Release** on GitHub (tag push returned HTTP 403 for the CI token).

Optional: delete the merged `kernel-v2` branch.

## 3. Repository map

```
README.md                     design-doc README (capabilities, results, diagrams, how-to-run)
Dockerfile                    Ubuntu 22.04 + gcc-multilib + QEMU build/test image
.github/workflows/ci.yml      CI: build image, run 5 suites, fuzz, cppcheck, CodeQL, regression gate
.github/codeql/config.yml     CodeQL config

src/                          the kernel
  threads/                    scheduler, synch, timer, kstat, trace, interrupt
  userprog/                   syscalls, process lifecycle, umem, ofile, pipe, rlimit
  vm/                         page.c frame.c swap.c mmap.c (demand paging)
  filesys/                    stock file system
  tests/ipc/                  NEW suite: pipes, dup2, redirect (+ .ck checkers, Make.tests)
  tests/limits/               NEW suite: fork-bomb, cpu-spin, fd-exhaust, mem-hog, neighbor, getlimit
  (tests/threads, tests/userprog, tests/vm, tests/filesys are stock — NEVER edited)

docs/
  adr/ADR-001..005            architecture decision records (written before the code)
  SYSCALLS.md                 all 19 system calls, semantics, POSIX comparison
  benchmarks/                 prebuilt GitHub Pages site (index.html + charts + demo.cast + player)
  index.html, .nojekyll       Pages landing page

analysis/
  PROGRESS.md                 milestone-by-milestone build log (authoritative narrative)
  INTERVIEW_PREP.md           deep-dive Q&A + STAR bug stories (per milestone)
  RESUME_EVIDENCE.md          claims C1–C9 → code + ADR + the test/measurement backing each
  PHASE1_REPORT.md            pre-work audit of the ORIGINAL course resume claims
  DIFF_SUMMARY.md             course code vs stock Pintos (baseline 9f013d0)
  PROJECT_REPORT.md           THIS FILE
  benchmarks/                 bench.jsonl, summary.csv, BENCHMARKS.md, SIMULATION.md, BUGS-FOUND.md, PNGs
  test-results/<milestone>/   raw committed logs per milestone (m0..m7 + stock/course baselines)
  analyze.py simulate.py build_site.py requirements.txt

scripts/
  test.sh / run-tests.sh      build + run a suite in Docker, emit summary + results.json
  bench.sh / bench-run.sh     the M7 eviction benchmark matrix
  make_demo_cast.py           build the M8 terminal demo from real test output
  fuzz.sh stress.sh           syscall fuzzer, stress runner
  trace2perfetto.py           kernel event trace → Perfetto JSON
  check_results.py            CI regression gate vs expected-results.json
  diff_vs_stock.py gcc-no-pie.sh
```

## 4. What was built, milestone by milestone

Each milestone re-ran **all** suites (no regressions) and recorded raw logs under
`analysis/test-results/`. Design decisions were written as ADRs **before** the code.

### M0 — baseline & tooling
Froze the course submission (tag `course-submission-2025-11`, archive branch). Dockerized the build
(Ubuntu 22.04, `-fno-pie`/`-no-pie` so the kernel doesn't triple-fault; `mirror.gcr.io` base to dodge
docker.io rate limits). `scripts/run-tests.sh` builds each project and emits machine-readable
`results.json`.

### M1 — correctness (scheduler + process lifecycle)
- Rewrote **priority donation** to handle nested/multiple donation correctly (`priority-donate-*`
  now pass); **threads 27/27** (stock 7/27).
- Redesigned the **process lifecycle** (ADR-002): parent/child records, `wait` works whether the
  child exits before or after, `exec` reports load failure via a load handshake, write-protected
  running executables. Added a global `filesys_lock`.

### M2 — pointer safety (ADR-001, `userprog/umem.c`)
Every user pointer is validated against the process page table before the kernel touches it — whole
range, across page boundaries, checked for writability. Removed the kernel panics the course version
had on bad pointers. **userprog 80/80**.

### M3 — real IPC (ADR-005; `tests/ipc`)
`pipe` (4 KB ring buffer + condition variables), `dup2`, `exec_redirect` (a `posix_spawn`-style fd
redirect), and open-file objects (`ofile.c`) so descriptors can be shared. A tiny shell (`minish`)
demonstrates pipelines. `docs/SYSCALLS.md` documents all calls vs POSIX. **ipc 11/11.**

### M4 — observability, fuzzing, CI
Kernel stat counters (`KSTAT` line at power-off), a **syscall fuzzer** (survivor + bomb children;
~125k calls, 0 panics), a stress runner, event tracing (`trace2perfetto.py` → Perfetto). **CI**
(GitHub Actions) builds the image, runs all suites, a short fuzz, **cppcheck**, **CodeQL**, and a
**regression gate** (`check_results.py` vs `expected-results.json`).

### M5 — resource limits / "cgroups-lite" (ADR-004; `tests/limits`)
Per-subtree **process** and **page** caps (shared with descendants) and per-process **fd** and
**CPU-time** budgets. A fork bomb is contained, a CPU hog is killed at its budget, a well-behaved
"neighbour" still meets its deadline beside both. **limits 6/6.**

### M6 — demand-paged virtual memory (ADR-003; `src/vm/`)
Four modules: supplemental page table (per process), frame table with **clock** eviction, swap over
`BLOCK_SWAP`, and memory-mapped files. Lazy loading, stack growth, eviction write-out, teardown.
Key correctness device: a **sticky `dirtied` flag** so a written anonymous page is always preserved
in swap (never zero-reconstructed). Deadlock avoided by **pinning** user buffers before taking
`filesys_lock`; lock order `frame_lock → filesys_lock/swap_lock`. **vm 113/113.**

### M7 — benchmarks + simulator + Pages  *(this session)*
- Made the eviction policy selectable at boot: **`-evict=clock|fifo|random`** (`vm/frame.c`,
  `threads/init.c`).
- `scripts/bench.sh` + `bench-run.sh`: run a **policy × workload × RAM × seed** matrix (5 seeds),
  recording the kernel's own `KSTAT` counters + timer ticks to `analysis/benchmarks/bench.jsonl`.
- `analysis/analyze.py`: aggregates mean±std, writes `BENCHMARKS.md` + charts.
- `analysis/simulate.py`: trace-driven **FIFO / LRU / CLOCK / OPT** simulator; reproduces **Belady's
  anomaly** and OPT as the lower bound (`SIMULATION.md`).
- `analysis/build_site.py`: assembles `docs/benchmarks/` (the Pages site) from live data.

### M8 — "watch it run" demo  *(this session)*
`scripts/make_demo_cast.py` builds an asciicast from the kernel's **real** M7 test output (fork bomb
contained, CPU hog killed, neighbour on time, VM swapping). `build_site.py` inlines it into a **tiny
self-contained vanilla-JS terminal player** (no CDN/external dependency) on the Pages site.

### M9 — portfolio capstone  *(this session)*
Rewrote `README.md` as a design document (capability table with evidence links, stock-vs-mine
results, the measured eviction result, Mermaid architecture + `read()` syscall diagrams, how-to-run,
AI-assisted authorship note). Added `RESUME_EVIDENCE.md` (claims C1–C9 each mapped to code + ADR +
test/measurement). Expanded `INTERVIEW_PREP.md`. Tagged `v2.0` (local). Marked the PR ready; merged
to `main`.

## 5. The measured result (the headline)

At a 4 MB memory budget that forces paging, same workload, different policy:

| workload | policy | evictions | swap-ins | note |
|---|---|---:|---:|---|
| page-parallel | **clock** | 164 | **3** | second-chance keeps hot pages |
| page-parallel | random | ~540 | ~360 | evicts by luck (high variance) |
| page-parallel | fifo | 480 | 320 | evicts hot pages by age |
| page-merge-par | clock | 334 | 179 | finishes in ~3 s |
| page-merge-par | fifo | — | — | **thrashes past the time budget (0/5 finish)** |

A **swap-in** is a page the policy evicted and immediately needed back, so clock's **3 vs FIFO's
~320** is the whole argument. Charts + full tables: `analysis/benchmarks/BENCHMARKS.md`; simulation
vs Belady optimal: `SIMULATION.md`. Reproduce with `scripts/bench-run.sh` then `python3
analysis/analyze.py`.

## 6. Bugs found and fixed by the benchmark  *(this session — important)*

Running the benchmark under parallel-fault stress exposed a nondeterministic crash (the parent
process dying at `eip=0` — a corrupted return address). Root cause: **two concurrency races** in the
paging path. Both fixed; full write-up in `analysis/benchmarks/BUGS-FOUND.md`.

1. **`swap_in` freed the slot before reading it** (`vm/swap.c`). A concurrent `swap_out` could
   reclaim the slot and overwrite it mid-read, mixing two pages. **Fix:** read the page out, *then*
   free the slot.
2. **Load during eviction** (`vm/frame.c` + `vm/page.c`). Between `page_evict` clearing the victim's
   PTE and recording `has_swap`, the owner could fault and the loader would zero-fill a page whose
   data was still heading to swap. **Fix:** an "evicting" barrier — the evictor marks the victim
   `evicting` under `frame_lock`; a faulting owner calls `frame_await_eviction` and waits on a
   condition variable until the evictor records where the data went.
3. (Minor) A CPU-budget kill didn't increment `KS_LIMIT_KILLS` (`threads/interrupt.c`) — an
   observability gap the M8 demo surfaced. Fixed; `cpu-spin` now reports `limit_kills=1`.

`page-parallel` went from flaky (0/5 at 5 MB) to 5/5; the full vm suite stays 113/113.

## 7. How to build and run

Everything runs in Docker (QEMU inside); no local toolchain needed.

```bash
docker build -t pintos-kernel .                 # build the image once

scripts/test.sh -l mylabel vm                   # run a suite -> analysis/test-results/mylabel/
                                                # suites: threads userprog ipc vm limits

scripts/bench-run.sh                            # the eviction benchmark (writes bench.jsonl)
python3 analysis/analyze.py                     # charts + BENCHMARKS.md (needs pandas, matplotlib)
python3 analysis/simulate.py                    # the simulator + SIMULATION.md
python3 analysis/build_site.py                  # rebuild docs/benchmarks/ site
python3 scripts/make_demo_cast.py               # rebuild the demo cast
```

**Gotcha for a future session:** when running a suite locally with `docker run ... pintos-kernel
<suite>`, the image has a *baked-in copy* of the source. If you've edited `src/` and not rebuilt the
image, **mount your working tree**: add `-v "$PWD/src:/pintos-src"`. CI rebuilds the image fresh, so
CI is always current. (This bit me once this session — a stale image still had an old `init.c`.)

## 8. Git & authorship state

- **main** has everything (merge commits `8e6e4c9` = PR #1 import, `6976d85` = PR #2 kernel-v2).
- **kernel-v2** is the dev branch (now merged); the task's designated dev branch was
  `claude/serene-newton-57q2qk` (the import branch, = PR #1 head), with kernel-v2 stacked on it.
- **archive/course-submission-2025-11** + tag `course-submission-2025-11` = the frozen course code.
- **v2.0** tag exists locally only (push blocked; owner to create the tag/Release).
- Every post-course commit carries `Co-Authored-By: Claude ...` and a `Claude-Session:` link. The
  README and `RESUME_EVIDENCE.md` state plainly that post-course work was AI-assisted.

## 9. Ground rules this project has followed (keep following them)

1. **Never edit stock `tests/` suites** (threads, userprog, vm, filesys). New tests go in **new**
   directories (`tests/ipc`, `tests/limits`) with their own `.c` + `.ck` + `Make.tests`, following
   Pintos conventions (14-char filename limit on the Pintos FS!).
2. **No copied solutions** — code written from Pintos docs and the codebase.
3. **No regressions** — every change re-runs all suites; raw logs committed under
   `analysis/test-results/`.
4. **ADR before code** — one `docs/adr/ADR-00x` per subsystem.
5. **Nothing on the résumé that isn't true here and explainable** (`INTERVIEW_PREP.md`).
6. **Authorship stays visible** — AI assistance disclosed per commit and in docs.
7. `#ifdef VM` guards so non-VM suites (userprog/ipc/limits) build without VM code.

## 10. Known limitations / honest gaps

- `exec` is `posix_spawn`-style, **not** a true `fork`/`execve` split, so an unmodified shell can't
  run; `minish` is a purpose-built demo shell.
- No hand-written x86 assembly is claimed; the work is C over the stock interrupt/TSS/page-table
  machinery.
- The Pintos base file system can't grow files (stock limitation); `create` sets a fixed size.
- Benchmark `fifo` runs on the write-heavy workload are recorded as **timeouts** by design (FIFO
  thrashes); that's a result, not a harness bug.

## 11. Suggested next phases (ideas for M10+)

These are options, not commitments — pick per portfolio goals.

- **M10 — swap read-ahead / a second-chance buffer cache**, measured the same way (the benchmark
  harness already supports new policies/workloads; add a `-cache` knob and a hit-rate counter).
- **M11 — a real `fork` + `execve`** so an unmodified shell and standard programs run (biggest
  "it's a real OS" signal; sizeable).
- **M12 — SMP / multi-core** scheduling (large; would make the lock-ordering work pay off visibly).
- **M13 — a writeback/extensible file system** (growable files, a buffer cache with its own
  eviction — reuse the M7 measurement story).
- **Smaller wins:** an eviction/fault event-ordering assertion in a debug build (hardens the race
  fix); a `--trace` capture feeding real reference strings into `simulate.py`; a flamegraph from the
  Perfetto traces.
- **Portfolio polish:** enable Pages + cut the v2.0 Release; a 1-paragraph case study and the two
  résumé bullets from `RESUME_EVIDENCE.md`.

## 12. Fast orientation for a future Claude session

- Read in this order: this file → `analysis/PROGRESS.md` → the relevant `docs/adr/` → the code.
- The authoritative "what passes" is `analysis/test-results/m7/` (latest full run, all 5 suites).
- To change the kernel and verify: edit `src/`, then `scripts/test.sh -l check <suite>` (it rebuilds
  the image) OR `docker run --entrypoint bash -v "$PWD/src:/pintos-src" pintos-kernel -c 'cd
  /pintos-src/<proj> && make && ...'` for a quick incremental loop.
- Develop on a feature branch, keep all suites green, write an ADR for any real design decision, and
  keep the AI-assisted authorship trailer on commits.
- Tag/Release and Pages are owner-only (token can't push tags); surface them as one-click asks.
