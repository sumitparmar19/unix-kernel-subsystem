#!/usr/bin/env python3
"""Convert a Pintos kernel trace dump into Chrome trace JSON for Perfetto.

Usage: trace2perfetto.py PINTOS_OUTPUT [-o trace.json]

PINTOS_OUTPUT is the console output of a run with the `-trace' kernel
option. The kernel prints TRACE-BEGIN / TRACE-CLOCK / TRACE-THREAD /
TRACE / TRACE-END lines at power-off (src/threads/trace.c). The result
opens directly in https://ui.perfetto.dev (or chrome://tracing).

Each kernel thread becomes a track:
  * "running" slices between context switches,
  * system calls and lock waits as nested slices,
  * pipe blocking as slices,
  * page faults, evictions, swapping and priority donation as instant events.
"""
import argparse
import json
import sys

TYPES = ["switch", "syscall_enter", "syscall_exit", "lock_wait", "lock_acquire",
         "donate", "page_fault", "evict", "swap_in", "swap_out", "pipe_block",
         "pipe_wake"]

SYSCALLS = ["halt", "exit", "exec", "wait", "create", "remove", "open",
            "filesize", "read", "write", "seek", "tell", "close", "mmap",
            "munmap", "chdir", "mkdir", "readdir", "isdir", "inumber", "pipe",
            "exec_redirect", "dup2", "setlimit", "getlimit", "uptime"]

PID = 1  # one process in the trace: the Pintos kernel


def parse(lines):
    clock, names, recs = None, {}, []
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "TRACE-CLOCK":
            tsc0, tick0, tsc1, tick1, freq = map(int, parts[1:6])
            clock = (tsc0, tick0, tsc1, tick1, freq)
        elif parts[0] == "TRACE-THREAD":
            names[int(parts[1])] = " ".join(parts[2:])
        elif parts[0] == "TRACE" and len(parts) == 5:
            recs.append(tuple(int(x) for x in parts[1:]))
    if clock is None:
        sys.exit("no TRACE-CLOCK line: was the kernel run with -trace?")
    return clock, names, recs


def convert(clock, names, recs):
    tsc0, tick0, tsc1, tick1, freq = clock
    seconds = (tick1 - tick0) / freq
    # TSC cycles per microsecond; fall back to 1 GHz for very short runs.
    rate = (tsc1 - tsc0) / (seconds * 1e6) if seconds > 0 and tsc1 > tsc0 else 1000.0
    start = recs[0][0] if recs else tsc0

    def us(tsc):
        return round((tsc - start) / rate, 3)

    events = [{"ph": "M", "pid": PID, "name": "process_name",
               "args": {"name": "Pintos kernel"}}]
    seen = set()

    def track(tid):
        if tid not in seen:
            seen.add(tid)
            name = names.get(tid % 256, "tid %d" % tid)
            events.append({"ph": "M", "pid": PID, "tid": tid, "name": "thread_name",
                           "args": {"name": "%s (tid %d)" % (name, tid)}})
        return tid

    running = {}  # tid -> start of its current "running" slice
    for tsc, tid, typ, arg in recs:
        t = us(tsc)
        kind = TYPES[typ] if typ < len(TYPES) else "type%d" % typ
        track(tid)
        if kind == "switch":
            if tid in running:
                begin = running.pop(tid)
                events.append({"ph": "X", "pid": PID, "tid": tid, "name": "running",
                               "cat": "sched", "ts": begin, "dur": round(t - begin, 3)})
            running[track(arg)] = t
        elif kind in ("syscall_enter", "syscall_exit"):
            name = SYSCALLS[arg] if arg < len(SYSCALLS) else "syscall %d" % arg
            events.append({"ph": "B" if kind == "syscall_enter" else "E", "pid": PID,
                           "tid": tid, "name": name, "cat": "syscall", "ts": t})
        elif kind == "lock_wait":
            events.append({"ph": "B", "pid": PID, "tid": tid, "cat": "lock", "ts": t,
                           "name": "lock wait (holder tid %d)" % arg})
        elif kind == "lock_acquire":
            events.append({"ph": "E", "pid": PID, "tid": tid, "cat": "lock", "ts": t})
        elif kind == "pipe_block":
            events.append({"ph": "B", "pid": PID, "tid": tid, "cat": "pipe", "ts": t,
                           "name": "pipe blocked (%s)" % ("write" if arg else "read")})
        elif kind == "pipe_wake":
            events.append({"ph": "E", "pid": PID, "tid": tid, "cat": "pipe", "ts": t})
        elif kind == "donate":
            events.append({"ph": "i", "s": "t", "pid": PID, "tid": tid, "cat": "sched",
                           "ts": t, "name": "donates priority %d" % arg})
        else:
            events.append({"ph": "i", "s": "t", "pid": PID, "tid": tid, "cat": "vm",
                           "ts": t, "name": "%s 0x%08x" % (kind.replace("_", " "), arg)})
    end = us(recs[-1][0]) if recs else 0
    for tid, ts in running.items():
        events.append({"ph": "X", "pid": PID, "tid": tid, "name": "running",
                       "cat": "sched", "ts": ts, "dur": round(end - ts, 3)})
    return {"traceEvents": events, "displayTimeUnit": "ms",
            "metadata": {"source": "Pintos kernel-v2 -trace",
                         "records": len(recs), "tsc_per_us": round(rate, 1)}}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input")
    ap.add_argument("-o", "--output", default="-")
    a = ap.parse_args()
    with open(a.input, errors="replace") as f:
        clock, names, recs = parse(f)
    doc = convert(clock, names, recs)
    out = sys.stdout if a.output == "-" else open(a.output, "w")
    json.dump(doc, out)
    if out is not sys.stdout:
        out.close()
        print("%s: %d records, %d events" % (a.output, len(recs), len(doc["traceEvents"])),
              file=sys.stderr)


if __name__ == "__main__":
    main()
