#!/usr/bin/env python3
"""Summarise the M7 page-replacement benchmark.

Reads analysis/benchmarks/bench.jsonl (produced by scripts/bench.sh) and
writes:
  * analysis/benchmarks/summary.csv      -- per-(workload,policy,mem) aggregates
  * analysis/benchmarks/BENCHMARKS.md    -- human-readable tables + commentary
  * analysis/benchmarks/evictions.png    -- policy comparison (evictions, swap-ins)
  * analysis/benchmarks/paging-curve.png -- clock evictions vs physical RAM

A run whose `exit` is null never reached a clean power-off (it hit the
wall-clock cap while thrashing); such runs are counted as timeouts and
excluded from the metric means but reported, because "did not finish in
budget" is itself a result.  Runs that powered off (exit 0 or -1) carry
real KSTAT counters and are included.

Requires pandas and matplotlib (see analysis/requirements.txt).
"""
import json
import pathlib
import sys

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = pathlib.Path(__file__).resolve().parent
BENCH = HERE / "benchmarks"
POLICY_ORDER = ["clock", "fifo", "random"]
POLICY_COLOR = {"clock": "#2563eb", "fifo": "#dc2626", "random": "#f59e0b"}
METRICS = ["evictions", "swap_ins", "swap_outs", "page_faults", "ctx_switches", "ticks"]


def load(path):
    rows = [json.loads(l) for l in path.read_text().splitlines() if l.strip()]
    if not rows:
        sys.exit(f"{path} is empty -- run scripts/bench-run.sh first")
    df = pd.DataFrame(rows)
    df["completed"] = df["exit"].notna()
    df["success"] = df["exit"] == 0
    df["timeout"] = df["exit"].isna()
    return df


def agg(df):
    """Mean/std of each metric over completed runs, plus run tallies."""
    g = df.groupby(["workload", "policy", "mem_mb"], sort=False)
    out = g.agg(
        runs=("seed", "size"),
        completed=("completed", "sum"),
        success=("success", "sum"),
        timeouts=("timeout", "sum"),
    )
    done = df[df["completed"]]
    gd = done.groupby(["workload", "policy", "mem_mb"], sort=False)
    for m in METRICS:
        out[f"{m}_mean"] = gd[m].mean()
        out[f"{m}_std"] = gd[m].std().fillna(0.0)
    return out.reset_index()


def pm(mean, std):
    if pd.isna(mean):
        return "--"
    if std == 0:
        return f"{mean:.0f}"
    return f"{mean:.0f}±{std:.0f}"


def md_table(sub, cols):
    head = "| " + " | ".join(c[0] for c in cols) + " |"
    rule = "|" + "|".join("---" for _ in cols) + "|"
    lines = [head, rule]
    for _, r in sub.iterrows():
        lines.append("| " + " | ".join(c[1](r) for c in cols) + " |")
    return "\n".join(lines)


def policy_chart(a):
    """Grouped bars: evictions and swap-ins by policy, per workload, at m=4."""
    m4 = a[a["mem_mb"] == 4]
    workloads = list(dict.fromkeys(m4["workload"]))
    fig, axes = plt.subplots(1, len(workloads), figsize=(5 * len(workloads), 4.2), squeeze=False)
    for ax, wl in zip(axes[0], workloads):
        sub = m4[m4["workload"] == wl].set_index("policy").reindex(POLICY_ORDER)
        x = range(len(POLICY_ORDER))
        ax.bar([i - 0.2 for i in x], sub["evictions_mean"], 0.4,
               yerr=sub["evictions_std"], label="evictions", color="#2563eb")
        ax.bar([i + 0.2 for i in x], sub["swap_ins_mean"], 0.4,
               yerr=sub["swap_ins_std"], label="swap-ins", color="#f59e0b")
        ax.set_xticks(list(x))
        ax.set_xticklabels(POLICY_ORDER)
        ax.set_title(f"{wl}  (4 MB RAM)")
        ax.set_ylabel("pages")
        ax.grid(axis="y", alpha=0.3)
        for i, pol in enumerate(POLICY_ORDER):
            to = int(sub.loc[pol, "timeouts"]) if pol in sub.index else 0
            if to:
                ax.text(i, 0, f"{to} timeout(s)", ha="center", va="bottom",
                        rotation=90, fontsize=8, color="#dc2626")
    axes[0][0].legend()
    fig.suptitle("Page-replacement policy: eviction work on the same workload")
    fig.tight_layout()
    fig.savefig(BENCH / "evictions.png", dpi=110)
    plt.close(fig)


def curve_chart(a):
    """Line: clock evictions vs physical RAM, per workload."""
    c = a[a["policy"] == "clock"].sort_values("mem_mb")
    fig, ax = plt.subplots(figsize=(6, 4.2))
    for wl in dict.fromkeys(c["workload"]):
        sub = c[c["workload"] == wl]
        ax.plot(sub["mem_mb"], sub["evictions_mean"], marker="o", label=wl)
    ax.set_xlabel("physical RAM (MB)")
    ax.set_ylabel("evictions (clock)")
    ax.set_title("Paging falls off as the working set starts to fit")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(BENCH / "paging-curve.png", dpi=110)
    plt.close(fig)


def main():
    df = load(BENCH / "bench.jsonl")
    a = agg(df)
    a.to_csv(BENCH / "summary.csv", index=False)
    policy_chart(a)
    curve_chart(a)

    m4 = a[a["mem_mb"] == 4]
    pol_cols = [
        ("workload", lambda r: r["workload"]),
        ("policy", lambda r: r["policy"]),
        ("runs", lambda r: f'{int(r["success"])}/{int(r["runs"])} ok'),
        ("evictions", lambda r: pm(r["evictions_mean"], r["evictions_std"])),
        ("swap-ins", lambda r: pm(r["swap_ins_mean"], r["swap_ins_std"])),
        ("swap-outs", lambda r: pm(r["swap_outs_mean"], r["swap_outs_std"])),
        ("page-faults", lambda r: pm(r["page_faults_mean"], r["page_faults_std"])),
        ("ticks", lambda r: pm(r["ticks_mean"], r["ticks_std"])),
    ]
    m4s = m4.sort_values(["workload", "policy"],
                         key=lambda s: s.map({p: i for i, p in enumerate(POLICY_ORDER)})
                         if s.name == "policy" else s)

    curve = a[a["policy"] == "clock"].sort_values(["workload", "mem_mb"])
    curve_cols = [
        ("workload", lambda r: r["workload"]),
        ("RAM (MB)", lambda r: f'{int(r["mem_mb"])}'),
        ("runs", lambda r: f'{int(r["success"])}/{int(r["runs"])} ok'),
        ("evictions", lambda r: pm(r["evictions_mean"], r["evictions_std"])),
        ("swap-ins", lambda r: pm(r["swap_ins_mean"], r["swap_ins_std"])),
        ("page-faults", lambda r: pm(r["page_faults_mean"], r["page_faults_std"])),
    ]

    nseeds = df["seed"].nunique()
    doc = f"""# Page-replacement benchmarks (M7)

Generated by `analysis/analyze.py` from `analysis/benchmarks/bench.jsonl`
(`scripts/bench-run.sh`). Each cell is mean±std over **{nseeds} seeds**;
counters are the kernel's own `KSTAT` line, `ticks` is Pintos timer ticks
(100 = 1 simulated second). "ok" counts runs that exited 0.

The kernel picks its eviction policy at boot with `-evict=clock|fifo|random`
(`src/vm/frame.c`, ADR-003). `clock` is second-chance (approximates LRU via
the hardware accessed bit); `fifo` evicts the oldest-allocated unpinned
frame; `random` evicts a random unpinned frame.

## Policy comparison at a fixed memory budget (4 MB RAM)

At 4 MB every workload pages heavily, so the policy decides how much I/O the
system does for the *same* program.

{md_table(m4s, pol_cols)}

![evictions by policy](evictions.png)

**Reading it.** Fewer evictions and especially fewer **swap-ins** is better: a
swap-in is a page the policy threw out and the program immediately needed
back. On `page-parallel` (sequential scans) clock evicts about 3x fewer pages
than FIFO and, decisively, swaps almost nothing back in (3 vs ~320) because the
accessed bit lets it keep the pages still in use; FIFO blindly evicts by age
and random by luck, so both pay far more swap traffic and run longer (random's
large standard deviation shows how unpredictable luck-based eviction is).

**Where clock does *not* win.** On the write-heavy `page-merge-par`, **random
slightly beats clock** (about 300 evictions / 145 swap-ins vs 334 / 179): a
merge sort rewrites its pages, so almost every frame's accessed bit is set and
clock's second-chance pass degenerates into an extra sweep before it evicts
roughly who FIFO/random would anyway — the bookkeeping is pure cost here. Clock
still wins decisively on the time budget: FIFO exceeds it on **every**
`page-merge-par` run (0/5 finish) and sometimes on `page-parallel` too (its
extra swap traffic makes run time erratic), while clock and random always
finish. The headline is "clock is the safe default," not "clock always has the
fewest evictions"; the "ok" counts above show how many runs of each config
finished within the budget.

## Paging curve: clock vs physical RAM

Holding the policy at clock and growing RAM shows the working set starting to
fit -- eviction work collapses toward zero.

{md_table(curve, curve_cols)}

![paging curve](paging-curve.png)

At 6 MB `page-parallel` and `page-merge-par` both reach **0 evictions**: the
whole working set is resident. Running this curve originally exposed two
concurrency bugs in the paging code (a free-before-read race in `swap_in` and
a load-during-eviction window) that only the benchmark's parallel stress
triggered; both are now fixed and every memory size passes. The diagnosis and
fixes are written up in `analysis/benchmarks/BUGS-FOUND.md`.

## How to reproduce

```bash
scripts/bench-run.sh            # writes analysis/benchmarks/bench.jsonl
python3 analysis/analyze.py     # regenerates this file + the PNGs
```
"""
    (BENCH / "BENCHMARKS.md").write_text(doc)
    print(f"wrote {BENCH/'BENCHMARKS.md'}, summary.csv, evictions.png, paging-curve.png")
    print(f"  {len(df)} runs, {int(df['success'].sum())} ok, "
          f"{int(df['timeout'].sum())} timeouts")


if __name__ == "__main__":
    main()
