#!/usr/bin/env python3
"""Assemble the static GitHub Pages benchmark site under docs/benchmarks/.

Reads analysis/benchmarks/summary.csv (from analyze.py) and copies the
generated charts, producing a single self-contained docs/benchmarks/index.html.
Numbers are pulled live from the CSV, so the page never drifts from the data.

Run after analyze.py and simulate.py:
    python3 analysis/build_site.py
"""
import csv
import html
import json
import pathlib
import shutil

HERE = pathlib.Path(__file__).resolve().parent
BENCH = HERE / "benchmarks"
SITE = HERE.parent / "docs" / "benchmarks"
REPO = "https://github.com/sumitparmar19/unix-kernel-subsystem"
BLOB = f"{REPO}/blob/kernel-v2"
POLICY_ORDER = {"clock": 0, "fifo": 1, "random": 2}


def load():
    with (BENCH / "summary.csv").open() as f:
        return list(csv.DictReader(f))


def num(r, k):
    v = r.get(k, "")
    return float(v) if v not in ("", "nan") else None


def cell(r, metric):
    m, s = num(r, f"{metric}_mean"), num(r, f"{metric}_std")
    if m is None:
        return "&mdash;"
    if not s:
        return f"{m:.0f}"
    return f"{m:.0f}<span class='pm'>&plusmn;{s:.0f}</span>"


def rows_for(data, pred):
    return [r for r in data if pred(r)]


def policy_table(data):
    m4 = sorted(rows_for(data, lambda r: r["mem_mb"] in ("4", "4.0")),
                key=lambda r: (r["workload"], POLICY_ORDER.get(r["policy"], 9)))
    head = ("<tr><th>workload</th><th>policy</th><th>ok</th><th>evictions</th>"
            "<th>swap-ins</th><th>swap-outs</th><th>page-faults</th><th>ticks</th></tr>")
    body = []
    for r in m4:
        ok = f'{int(float(r["success"]))}/{int(float(r["runs"]))}'
        klass = " class='win'" if r["policy"] == "clock" else ""
        body.append(
            f"<tr{klass}><td>{html.escape(r['workload'])}</td>"
            f"<td><code>{r['policy']}</code></td><td>{ok}</td>"
            f"<td>{cell(r,'evictions')}</td><td>{cell(r,'swap_ins')}</td>"
            f"<td>{cell(r,'swap_outs')}</td><td>{cell(r,'page_faults')}</td>"
            f"<td>{cell(r,'ticks')}</td></tr>")
    return head + "".join(body)


def curve_table(data):
    c = sorted(rows_for(data, lambda r: r["policy"] == "clock"),
               key=lambda r: (r["workload"], float(r["mem_mb"])))
    head = ("<tr><th>workload</th><th>RAM (MB)</th><th>ok</th><th>evictions</th>"
            "<th>swap-ins</th><th>page-faults</th></tr>")
    body = []
    for r in c:
        ok = f'{int(float(r["success"]))}/{int(float(r["runs"]))}'
        body.append(
            f"<tr><td>{html.escape(r['workload'])}</td>"
            f"<td>{int(float(r['mem_mb']))}</td><td>{ok}</td>"
            f"<td>{cell(r,'evictions')}</td><td>{cell(r,'swap_ins')}</td>"
            f"<td>{cell(r,'page_faults')}</td></tr>")
    return head + "".join(body)


TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Page-replacement benchmarks &middot; unix-kernel-subsystem</title>
<style>
  :root {{
    --bg:#ffffff; --fg:#1f2328; --muted:#636c76; --line:#d0d7de;
    --accent:#2563eb; --win:#eef6ff; --card:#f6f8fa; --code:#eaeef2;
  }}
  @media (prefers-color-scheme: dark) {{
    :root:not([data-theme="light"]) {{
      --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1; --line:#30363d;
      --accent:#539bf5; --win:#12243b; --card:#161b22; --code:#1f2630;
    }}
  }}
  :root[data-theme="dark"] {{
    --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1; --line:#30363d;
    --accent:#539bf5; --win:#12243b; --card:#161b22; --code:#1f2630;
  }}
  * {{ box-sizing:border-box; }}
  body {{ margin:0; background:var(--bg); color:var(--fg);
    font:16px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif; }}
  main {{ max-width:860px; margin:0 auto; padding:48px 16px 96px; }}
  h1 {{ font-size:2rem; margin:0 0 .2em; }}
  h2 {{ margin-top:2.4em; padding-bottom:.3em; border-bottom:1px solid var(--line); }}
  .sub {{ color:var(--muted); font-size:1.05rem; }}
  a {{ color:var(--accent); }}
  code {{ background:var(--code); padding:.12em .4em; border-radius:5px;
    font:.88em ui-monospace,SFMono-Regular,Menlo,monospace; }}
  figure {{ margin:1.5em 0; }}
  img {{ width:100%; height:auto; border:1px solid var(--line); border-radius:10px;
    background:#fff; }}
  figcaption {{ color:var(--muted); font-size:.9rem; margin-top:.5em; }}
  table {{ border-collapse:collapse; width:100%; margin:1.2em 0; font-size:.92rem;
    display:block; overflow-x:auto; }}
  th,td {{ border:1px solid var(--line); padding:7px 11px; text-align:right;
    white-space:nowrap; }}
  th:first-child,td:first-child,th:nth-child(2),td:nth-child(2) {{ text-align:left; }}
  th {{ background:var(--card); }}
  tr.win td {{ background:var(--win); }}
  .pm {{ color:var(--muted); font-size:.82em; }}
  .cards {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr));
    gap:12px; margin:1.5em 0; }}
  .card {{ background:var(--card); border:1px solid var(--line); border-radius:10px;
    padding:16px; }}
  .card b {{ display:block; font-size:1.7rem; line-height:1.1; }}
  .card span {{ color:var(--muted); font-size:.85rem; }}
  footer {{ margin-top:4em; padding-top:1.5em; border-top:1px solid var(--line);
    color:var(--muted); font-size:.88rem; }}
  .note {{ background:var(--card); border-left:3px solid var(--accent);
    padding:.8em 1em; border-radius:0 8px 8px 0; margin:1.3em 0; }}
  #term {{ margin:1.3em 0; background:#0d1117; color:#c9d1d9; border-radius:10px;
    padding:16px 18px; font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
    white-space:pre-wrap; word-break:break-word; min-height:22em; overflow:hidden;
    border:1px solid var(--line); }}
  #term b {{ font-weight:700; }}
  .t-g {{ color:#3fb950; }} .t-c {{ color:#8b949e; }} .t-y {{ color:#d29922; }}
  .t-r {{ color:#f85149; }}
  .replay {{ font-size:.8rem; color:var(--muted); margin:-.6em 0 0; }}
</style>
</head>
<body>
<main>
  <h1>Page-replacement benchmarks</h1>
  <p class="sub">Measuring eviction-policy trade-offs in a demand-paged
  Pintos kernel &mdash; part of <a href="{repo}">unix-kernel-subsystem</a>.</p>

  <p>The kernel pages on demand and chooses its eviction policy at boot
  (<code>-evict=clock|fifo|random</code>). These numbers are the kernel's own
  <code>KSTAT</code> counters over {seeds} seeds per configuration, collected by
  <a href="{blob}/scripts/bench.sh"><code>scripts/bench.sh</code></a> and
  summarised by
  <a href="{blob}/analysis/analyze.py"><code>analyze.py</code></a>.
  Design rationale is in
  <a href="{blob}/docs/adr/ADR-003-virtual-memory.md">ADR-003</a>.</p>

  <div class="cards">
    <div class="card"><b>{pp_ratio}&times;</b><span>fewer evictions under
      clock than FIFO on <code>page-parallel</code> (4&nbsp;MB)</span></div>
    <div class="card"><b>{pp_clock_si}</b><span>pages swapped back in under
      clock vs {pp_fifo_si} under FIFO &mdash; clock keeps the hot set</span></div>
    <div class="card"><b>&infin;</b><span>FIFO thrashes
      <code>page-merge-par</code> past the time budget; clock finishes in
      ~3&nbsp;s</span></div>
  </div>

  <h2>Watch it run</h2>
  <p>A replay of the kernel's real output: a fork bomb contained, a CPU hog
  killed at its budget, a well-behaved process still finishing on time beside
  both, and demand-paged VM swapping a working set larger than RAM.</p>
  <div id="term"></div>
  <p class="replay">Recorded kernel output, replayed. Click to pause/resume.</p>

  <h2>Policy comparison at a fixed memory budget (4&nbsp;MB)</h2>
  <p>At 4&nbsp;MB every workload pages heavily, so the policy alone decides how
  much I/O the system does for the <em>same</em> program. Fewer swap-ins is
  better &mdash; a swap-in is a page the policy evicted and the program
  immediately needed back.</p>
  <figure><img src="evictions.png" alt="evictions and swap-ins by policy">
    <figcaption>Eviction work by policy. Clock (blue, highlighted) uses the
    accessed bit to keep pages still in use; FIFO and random pay far more swap
    traffic.</figcaption></figure>
  <table>{policy_table}</table>

  <h2>Paging curve: clock vs physical RAM</h2>
  <p>Holding the policy at clock and growing RAM shows the working set starting
  to fit &mdash; eviction collapses to zero once it is resident.</p>
  <figure><img src="paging-curve.png" alt="evictions vs physical RAM">
    <figcaption>Evictions fall to zero by 6&nbsp;MB as the working set becomes
    resident.</figcaption></figure>
  <table>{curve_table}</table>
  <div class="note">Running this curve originally surfaced two concurrency
  bugs in the paging code &mdash; a free-before-read race in <code>swap_in</code>
  and a load-during-eviction window &mdash; that only the benchmark's parallel
  stress triggered. Both are fixed; every memory size now passes. Write-up:
  <a href="{blob}/analysis/benchmarks/BUGS-FOUND.md">BUGS-FOUND.md</a>.</div>

  <h2>Theory: simulation against Belady's optimal</h2>
  <p>A trace-driven simulator
  (<a href="{blob}/analysis/simulate.py"><code>simulate.py</code></a>) runs the
  same reference strings under FIFO, LRU, CLOCK and OPT (Belady's optimal, the
  unbeatable lower bound). It reproduces Belady's anomaly &mdash; FIFO faulting
  <em>more</em> with more frames &mdash; which LRU and OPT, as stack algorithms,
  never do.</p>
  <figure><img src="simulate.png" alt="page faults vs frames per policy">
    <figcaption>OPT is the floor; the gap above it is the cost of not knowing
    the future. Full tables in
    <a href="{blob}/analysis/benchmarks/SIMULATION.md">SIMULATION.md</a>.</figcaption></figure>

  <footer>
    Generated by <code>analysis/build_site.py</code> from measured data.
    Source: <a href="{repo}">{repo_short}</a> &middot;
    <a href="{blob}/analysis/benchmarks/BENCHMARKS.md">raw benchmark write-up</a>.
    repository README.
  </footer>
</main>
__PLAYER__
</body>
</html>
"""

# A tiny self-contained asciicast v2 player: no external JS, cast inlined.
# Parses the SGR colours the cast uses (0/1/31/32/33/36) into spans.
PLAYER = """<script>
(function () {
  var EV = __CAST__;
  var term = document.getElementById('term');
  var SGR = {31:'t-r',32:'t-g',33:'t-y',36:'t-c'};
  function render(raw) {
    var out = '', i = 0, cls = '', bold = false, open = false;
    function close() { if (open) { out += '</span>'; open = false; } }
    function esc(c){ return c==='&'?'&amp;':c==='<'?'&lt;':c==='>'?'&gt;':c; }
    while (i < raw.length) {
      if (raw[i] === '\\x1b' && raw[i+1] === '[') {
        var j = raw.indexOf('m', i);
        if (j > 0) {
          raw.slice(i+2, j).split(';').forEach(function (n) {
            n = parseInt(n||'0', 10);
            if (n === 0) { cls=''; bold=false; }
            else if (n === 1) bold = true;
            else if (SGR[n]) cls = SGR[n];
          });
          close();
          if (cls || bold) {
            out += '<span class="' + cls + '"' + (bold?' style="font-weight:700"':'') + '>';
            open = true;
          }
          i = j + 1; continue;
        }
      }
      out += esc(raw[i]); i++;
    }
    close();
    return out;
  }
  var raw = '', k = 0, timer = null, paused = false;
  function step() {
    if (k >= EV.length) {           // loop
      setTimeout(function () { raw=''; k=0; term.innerHTML=''; step(); }, 2500);
      return;
    }
    raw += EV[k][2];
    term.innerHTML = render(raw);
    var cur = EV[k][0], nxt = (k+1 < EV.length) ? EV[k+1][0] : cur;
    k++;
    var d = Math.min((nxt - cur) * 1000, 1200);
    timer = setTimeout(step, Math.max(d, 0));
  }
  term.addEventListener('click', function () {
    paused = !paused;
    if (paused) clearTimeout(timer); else step();
  });
  step();
})();
</script>"""


def main():
    data = load()
    SITE.mkdir(parents=True, exist_ok=True)
    for png in ("evictions.png", "paging-curve.png", "simulate.png"):
        src = BENCH / png
        if src.exists():
            shutil.copy2(src, SITE / png)

    # Headline figures from the page-parallel 4 MB rows.
    def find(wl, pol):
        for r in data:
            if r["workload"] == wl and r["policy"] == pol and r["mem_mb"] in ("4", "4.0"):
                return r
        return None
    pc, pf = find("page-parallel", "clock"), find("page-parallel", "fifo")
    pc_ev = num(pc, "evictions_mean") or 1
    pf_ev = num(pf, "evictions_mean") or 1
    seeds = int(float(pc["runs"])) if pc else 5

    htmltext = TEMPLATE.format(
        repo=REPO, blob=BLOB, repo_short=REPO.replace("https://github.com/", ""),
        seeds=seeds,
        pp_ratio=f"{pf_ev / pc_ev:.1f}",
        pp_clock_si=f"{num(pc,'swap_ins_mean'):.0f}" if pc else "?",
        pp_fifo_si=f"{num(pf,'swap_ins_mean'):.0f}" if pf else "?",
        policy_table=policy_table(data),
        curve_table=curve_table(data),
    )

    # Inline the recorded demo cast into the self-contained player.
    cast_path = BENCH / "demo.cast"
    if cast_path.exists():
        lines = [l for l in cast_path.read_text().splitlines() if l.strip()]
        events = [json.loads(l) for l in lines[1:]]   # skip the header line
        cast_json = json.dumps(events, ensure_ascii=True)
        htmltext = htmltext.replace("__PLAYER__", PLAYER.replace("__CAST__", cast_json))
    else:
        htmltext = htmltext.replace("__PLAYER__", "")

    (SITE / "index.html").write_text(htmltext)
    print(f"wrote {SITE/'index.html'} and charts")


if __name__ == "__main__":
    main()
