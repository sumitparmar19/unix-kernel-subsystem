#!/usr/bin/env python3
"""Generate analysis/benchmarks/demo.cast -- an asciicast v2 recording of the
kernel running, inlined into the benchmark site's self-contained player by
analysis/build_site.py.

The terminal output below is the *real* output the kernel produced in the
M7 test runs (analysis/test-results/m7/.../*.output) and the M7 benchmark
(analysis/benchmarks/bench.jsonl); this script only adds playback timing and
prompt framing so a visitor can watch it. It is a replay, not a live shell.

    python3 scripts/make_demo_cast.py    # writes docs/benchmarks/demo.cast
"""
import json
import pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "analysis" / "benchmarks" / "demo.cast"
WIDTH, HEIGHT = 92, 32

# ANSI colours.
G = "\u001b[32m"      # green (prompt / commands)
C = "\u001b[36m"      # cyan (comments)
Y = "\u001b[33m"      # yellow (kernel test labels)
R = "\u001b[31m"      # red (a killed process)
B = "\u001b[1m"       # bold
Z = "\u001b[0m"       # reset

PROMPT = f"{G}pintos${Z} "

events = []
t = [0.0]


def emit(s, dt=0.0):
    t[0] += dt
    events.append([round(t[0], 3), "o", s])


def line(s="", dt=0.09):
    emit(s + "\r\n", dt)


def comment(s):
    line(f"{C}# {s}{Z}", 0.5)


def typed(cmd):
    """Type a command out character by character, then press enter."""
    emit(PROMPT, 0.4)
    for ch in cmd:
        emit(ch, 0.028)
    emit("\r\n", 0.35)


def kernel(lines, dt=0.11):
    for ln in lines:
        line(ln, dt)


line(f"{B}unix-kernel-subsystem{Z}  —  running many untrusted programs safely on one machine", 0.2)
line("", 0.2)

comment("1) A fork bomb is contained by the per-subtree process limit, not fatal")
typed("pintos -- run fork-bomb")
kernel([
    f"{Y}(fork-bomb){Z} begin",
    f"{Y}(fork-bomb){Z} set process limit to 8",
    f"{Y}(fork-bomb){Z} exec was capped at 7 children",
] + [f"child-block: exit(0)"] * 7 + [
    f"{Y}(fork-bomb){Z} all 7 contained children exited cleanly",
    f"{Y}(fork-bomb){Z} kernel still works after the bomb",
    "fork-bomb: exit(0)",
    "KSTAT syscalls=42 ctx_switches=1266 page_faults=12 limit_denials=1 limit_kills=0",
])
line("", 0.6)

comment("2) A CPU hog is killed when it burns its scheduling budget")
typed("pintos -- run cpu-spin")
kernel([
    f"{Y}(cpu-spin){Z} set CPU budget to 50 ticks",
    f"{Y}(cpu-spin){Z} spinning; the kernel should kill us at the budget",
    f"{R}cpu-spin: exit(-1){Z}",
    "KSTAT syscalls=4 ctx_switches=814 page_faults=4 limit_denials=0 limit_kills=1",
])
line("", 0.6)

comment("3) A well-behaved process still finishes ON TIME next to both abusers")
typed("pintos -- run neighbor")
kernel([
    f"{Y}(neighbor){Z} started the two abusers (a fork bomb and a CPU spinner)",
    f"{Y}(neighbor){Z} started the well-behaved neighbour",
    f"{Y}(neighbor){Z} neighbour computed the right answer",
    f"{B}{Y}(neighbor){Z}{B} neighbour finished within the deadline{Z}",
    f"{R}abuser-spin: exit(-1){Z}",
    "abuser-fork: exit(0)",
    "neighbor: exit(0)",
])
line("", 0.6)

comment("4) Demand-paged VM: a working set larger than RAM, paged to swap")
typed("pintos -m 4 -- run page-parallel   # 4 processes, 4 MB RAM")
kernel([
    f"{Y}(page-parallel){Z} begin",
    f"{Y}(page-parallel){Z} exec \"child-linear\"  x4",
    "child-linear: exit(66)   (x4)",
    f"{Y}(page-parallel){Z} wait for child 0..3",
    f"{Y}(page-parallel){Z} end",
    "page-parallel: exit(0)",
    f"{B}KSTAT evictions=164 swap_ins=3 swap_outs=159{Z}   <- clock keeps the hot pages",
])
line("", 0.6)

comment("Full regression, every suite green:")
line(f"{G}  threads 27/27   userprog 80/80   ipc 11/11   vm 113/113   limits 6/6{Z}", 0.2)
line("", 0.2)
comment("See the measured eviction-policy comparison above. Source on GitHub.")
emit(PROMPT, 0.5)
emit("█", 0.1)   # cursor block, lingers
t[0] += 3.0

header = {"version": 2, "width": WIDTH, "height": HEIGHT,
          "title": "unix-kernel-subsystem demo",
          "env": {"TERM": "xterm-256color"}}
with OUT.open("w") as f:
    f.write(json.dumps(header) + "\n")
    for e in events:
        f.write(json.dumps(e) + "\n")
print(f"wrote {OUT} ({len(events)} events, {t[0]:.1f}s)")
