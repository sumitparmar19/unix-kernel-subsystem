#!/usr/bin/env python3
"""Trace-driven page-replacement simulator.

Runs FIFO, LRU, CLOCK (second-chance) and OPT (Belady's optimal) over the
same page-reference string and counts page faults for a range of frame
counts.  This puts the kernel's measured numbers (analysis/benchmarks/) next
to theory: OPT is the unbeatable lower bound no online policy can reach, and
FIFO exhibits Belady's anomaly (more frames -> *more* faults) on the classic
reference string, which clock and LRU never do.

Reference strings come from either:
  * a trace file: one integer page number per line (e.g. extracted from a
    kernel run), passed with --trace FILE; or
  * built-in synthetic patterns (--pattern), which model the benchmark
    workloads and the textbook Belady string.

Writes analysis/benchmarks/SIMULATION.md and simulate.png.
Requires matplotlib for the chart (the simulation itself is pure stdlib).
"""
import argparse
import pathlib
import random

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = pathlib.Path(__file__).resolve().parent
BENCH = HERE / "benchmarks"


# ----- policies: each returns the number of page faults -----------------
def fifo(refs, nframes):
    frames, queue, faults = set(), [], 0
    for p in refs:
        if p in frames:
            continue
        faults += 1
        if len(frames) >= nframes:
            victim = queue.pop(0)
            frames.discard(victim)
        frames.add(p)
        queue.append(p)
    return faults


def lru(refs, nframes):
    frames, recent, faults = set(), [], 0
    for p in refs:
        if p in frames:
            recent.remove(p)
            recent.append(p)
            continue
        faults += 1
        if len(frames) >= nframes:
            victim = recent.pop(0)
            frames.discard(victim)
        frames.add(p)
        recent.append(p)
    return faults


def clock(refs, nframes):
    """Second-chance: a circular buffer of (page, ref-bit)."""
    buf, hand, faults = [], 0, 0
    present = {}
    for p in refs:
        if p in present:
            present[p][1] = 1           # set reference bit
            continue
        faults += 1
        if len(buf) < nframes:
            slot = [p, 1]
            buf.append(slot)
            present[p] = slot
        else:
            while buf[hand][1] == 1:
                buf[hand][1] = 0
                hand = (hand + 1) % nframes
            victim = buf[hand][0]
            del present[victim]
            slot = [p, 1]
            buf[hand] = slot
            present[p] = slot
            hand = (hand + 1) % nframes
    return faults


def opt(refs, nframes):
    """Belady's optimal: evict the page whose next use is farthest away."""
    frames, faults = [], 0
    for i, p in enumerate(refs):
        if p in frames:
            continue
        faults += 1
        if len(frames) >= nframes:
            # For each resident page, find its next use after i.
            def next_use(q):
                try:
                    return refs.index(q, i + 1)
                except ValueError:
                    return float("inf")
            victim = max(frames, key=next_use)
            frames.remove(victim)
        frames.append(p)
    return faults


POLICIES = {"OPT": opt, "LRU": lru, "CLOCK": clock, "FIFO": fifo}
COLOR = {"OPT": "#16a34a", "LRU": "#2563eb", "CLOCK": "#7c3aed", "FIFO": "#dc2626"}


# ----- synthetic reference strings --------------------------------------
def pattern(name, seed=1):
    rng = random.Random(seed)
    if name == "belady":
        # Classic string where FIFO faults *more* with 4 frames than 3.
        return [1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5]
    if name == "sequential":
        # page-parallel-like: scan a 40-page array a few times in order.
        return [p for _ in range(4) for p in range(40)]
    if name == "loop":
        # Working set slightly larger than memory, repeatedly looped: the
        # pathological case for FIFO/clock, where LRU/OPT hold the set.
        return [p for _ in range(8) for p in range(12)]
    if name == "zipf":
        # Skewed (a few hot pages): models a mergesort's hot working set.
        pages, out = 50, []
        weights = [1.0 / (i + 1) for i in range(pages)]
        out = rng.choices(range(pages), weights=weights, k=600)
        return out
    raise SystemExit(f"unknown pattern {name}")


def simulate(refs, frame_range):
    return {name: [fn(refs, n) for n in frame_range] for name, fn in POLICIES.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", type=pathlib.Path,
                    help="page-number trace file (one int per line)")
    ap.add_argument("--pattern", default="all",
                    help="synthetic pattern or 'all' (belady,sequential,loop,zipf)")
    args = ap.parse_args()

    if args.trace:
        refs = [int(x) for x in args.trace.read_text().split()]
        datasets = {args.trace.name: refs}
    elif args.pattern == "all":
        datasets = {n: pattern(n) for n in ("belady", "sequential", "loop", "zipf")}
    else:
        datasets = {args.pattern: pattern(args.pattern)}

    fig, axes = plt.subplots(1, len(datasets), figsize=(4.6 * len(datasets), 4.2),
                             squeeze=False)
    md = ["# Trace-driven page-replacement simulation\n",
          "Generated by `analysis/simulate.py`. Page faults for each policy over "
          "the same reference string, sweeping the number of physical frames. "
          "**OPT** (Belady's optimal) is the lower bound no online policy can "
          "beat; the gap above it is the price of not knowing the future.\n",
          "\n> These reference strings are **synthetic** -- textbook patterns "
          "(`belady`, `sequential`, `loop`, `zipf`) chosen to show each policy's "
          "character, not traces captured from the kernel. Pass `--trace FILE` to "
          "feed a real captured reference string instead.\n"]

    for ax, (name, refs) in zip(axes[0], datasets.items()):
        hi = min(len(set(refs)) + 2, 12)
        frame_range = list(range(2, hi + 1))
        res = simulate(refs, frame_range)
        for pol in POLICIES:
            ax.plot(frame_range, res[pol], marker="o", label=pol, color=COLOR[pol])
        ax.set_title(f"{name}  ({len(refs)} refs, {len(set(refs))} pages)")
        ax.set_xlabel("frames")
        ax.set_ylabel("page faults")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)

        md.append(f"\n## `{name}` ({len(refs)} references, "
                  f"{len(set(refs))} distinct pages)\n")
        header = "| frames | " + " | ".join(POLICIES) + " |"
        md.append(header)
        md.append("|" + "|".join("---" for _ in range(len(POLICIES) + 1)) + "|")
        for i, n in enumerate(frame_range):
            md.append(f"| {n} | " + " | ".join(str(res[p][i]) for p in POLICIES) + " |")
        # Belady anomaly: more frames -> more faults.  Only LRU and OPT are
        # stack algorithms and so provably cannot show it; FIFO and CLOCK can.
        notes = []
        for pol in ("FIFO", "CLOCK"):
            r = res[pol]
            bad = [(frame_range[i - 1], frame_range[i])
                   for i in range(1, len(r)) if r[i] > r[i - 1]]
            if bad:
                notes.append(f"**{pol}** at " +
                             ", ".join(f"{a}->{b} frames" for a, b in bad))
        if notes:
            md.append("\n> **Belady's anomaly** (more frames, *more* faults): "
                      + "; ".join(notes) + ". LRU and OPT are stack algorithms "
                      "and never show it; FIFO and CLOCK are not and can.")

    fig.suptitle("Page faults vs frames: OPT is the floor, FIFO can misbehave")
    fig.tight_layout()
    fig.savefig(BENCH / "simulate.png", dpi=110)
    plt.close(fig)

    md.append("\n## Connection to the kernel\n")
    md.append("The kernel defaults to CLOCK (`src/vm/frame.c`). The simulation "
              "shows why: CLOCK tracks LRU closely using just one reference bit "
              "per frame instead of LRU's full ordering, so it is cheap enough "
              "to run in the fault path, yet it stays far below FIFO on skewed "
              "and looping references. CLOCK is not a true stack algorithm and "
              "so can still show Belady's anomaly on adversarial strings (above), "
              "and it is the safe default on real workloads: in the measured "
              "runs (`BENCHMARKS.md`) FIFO thrashes the write-heavy mergesort "
              "past the time budget while clock always finishes. Clock is not "
              "always the fewest-faults policy -- on that same mergesort random "
              "edges it out, because rewriting every page defeats the "
              "accessed-bit heuristic -- but it never loses badly, which FIFO "
              "does.\n")
    md.append("Reproduce: `python3 analysis/simulate.py` "
              "(or `--trace FILE` for a captured reference string).\n")
    (BENCH / "SIMULATION.md").write_text("\n".join(md))
    print(f"wrote {BENCH/'SIMULATION.md'} and simulate.png over {len(datasets)} dataset(s)")


if __name__ == "__main__":
    main()
