# Stock-Pintos baseline (not Sumit's code)

The course handout **without Sumit's changes**: Stanford `pintos-anon` commit `9f013d0` plus the
course's `utils/pintos` (QEMU default). Run on 2026-10-15 with this repo's `Dockerfile` and
`scripts/run-tests.sh` (QEMU, `make -k check -j4`). Use it to see which passes come free with the
handout and which come from Sumit's work.

| Suite | Stock passes | Sumit's tree |
|---|---|---|
| threads  | **7 / 27**: `alarm-single`, `alarm-multiple`, `alarm-simultaneous`, `alarm-zero`, `alarm-negative` (the stock busy-wait `timer_sleep` still produces correct output), `mlfqs-fair-2`, `mlfqs-fair-20` (equal-priority threads get equal CPU under round robin) | 26 / 27 |
| userprog | **0 / 80**: the kernel boots and loads the program, but there are no system calls and `process_wait` returns at once, so no test produces output | 63 / 80 |

Each folder keeps `summary.txt`, `verdicts.txt`, `check.log`, `grade` and `build.log`. Per-test
outputs were dropped to keep the repo small; re-run to regenerate them.
