# Stock-Pintos baseline (not Sumit's code)

These results come from an **unmodified** Pintos tree, run through this repo's
`Dockerfile` and `scripts/run-tests.sh` on 2026-10-14. They serve two purposes:

1. They prove the Docker toolchain builds Pintos and runs every suite end to end.
2. They show how many tests pass **with zero student work**, so Sumit's own
   results can be credited correctly. For example, 7 threads tests pass on stock code.

Tree used: `jhu-cs318/pintos` at `ab69995` (a widely used Stanford-Pintos
derivative), with this repo's root `Make.config`, `Makefile.build`,
`Makefile.kernel` and `Makefile.userprog` copied over it. This is **not** the
exact handout Sumit's course used, so test totals may differ slightly from
his tree. All suites ran under QEMU 6.2 (`SIMULATOR=--qemu`), `make -k check -j4`.

| Project | Passed (stock) | Notes |
|---|---|---|
| threads  | 7 / 27   | `alarm-single`, `alarm-multiple`, `alarm-simultaneous`, `alarm-zero`, `alarm-negative` pass with the stock busy-wait `timer_sleep`; `mlfqs-fair-2` and `mlfqs-fair-20` pass because all threads have equal priority |
| userprog | 0 / 80   | Kernel boots and loads the program, but stock `process_wait` returns at once and no system calls exist, so the tests report "Run didn't produce any output" |
| vm       | 0 / 113  | Same reason as userprog |
| filesys  | 0 / 102  | 81 fail; the other 21 (`tests/filesys/extended/*`) get no verdict because their persistence step can't run without working system calls |

Each project folder holds `summary.txt`, `verdicts.txt` (one PASS/FAIL line
per test) and the raw `check.log`.
