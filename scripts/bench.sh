#!/usr/bin/env bash
# Page-replacement benchmark.  Runs a matrix of
#   eviction policy x workload x physical-RAM x random-seed
# against the demand-paged VM kernel and records, for each run, the
# kernel's own KSTAT counters (evictions, swap-ins/outs, page faults,
# context switches) and its Timer-tick run time.  One JSON object per
# run is appended to $RESULTS_DIR/bench.jsonl; analysis/analyze.py turns
# that into tables and charts.
#
# Runs INSIDE the pintos-kernel container (needs pintos + qemu on PATH):
#   docker run --rm --entrypoint bash -v "$PWD/src:/pintos-src" \
#       -v "$OUT:/results" pintos-kernel /pintos-src/../scripts/bench.sh
# or simply scripts/bench-run.sh from the host (builds the image, mounts).
set -u

SRC="${PINTOS_SRC:-/pintos-src}"
RESULTS_DIR="${RESULTS_DIR:-/results}"
SEEDS="${SEEDS:-1 2 3 4 5}"
TIMEOUT="${BENCH_TIMEOUT:-120}"        # pintos CPU-time cap (simulated)
WALL="${BENCH_WALL:-45}"               # hard wall-clock cap per run; a run that
                                       # blows it (FIFO thrashing page-merge-par)
                                       # is recorded as a timeout -- itself a result
mkdir -p "$RESULTS_DIR"
out="$RESULTS_DIR/bench.jsonl"
: >"$out"

# Build the VM kernel once.
echo "=== building vm kernel"
if ! make -C "$SRC/vm" >"$RESULTS_DIR/bench-build.log" 2>&1; then
  echo "BUILD FAILED (see bench-build.log)" >&2
  exit 1
fi
build="$SRC/vm/build"

# Workload table: name | swap MB | extra -p/-a child args (space separated).
# page-parallel  : four children each scan a large anonymous array once
#                  (sequential / read-heavy).
# page-merge-par : parallel merge sort across children (write-heavy, dirties
#                  pages, so eviction must write back to swap).
declare -A SWAP CHILD
SWAP[page-parallel]=4;   CHILD[page-parallel]="-p tests/vm/child-linear -a child-linear"
SWAP[page-merge-par]=10; CHILD[page-merge-par]="-p tests/vm/child-sort -a child-sort"

# run POLICY WORKLOAD MEM SEED  -> one JSON line.
run_one () {
  local pol=$1 wl=$2 mem=$3 seed=$4
  local sw=${SWAP[$wl]} child=${CHILD[$wl]}
  local raw exit_s ticks ks
  raw=$(cd "$build" && timeout "$WALL" pintos -v -k -T "$TIMEOUT" --qemu -m "$mem" \
        --filesys-size=2 --swap-size="$sw" \
        -p "tests/vm/$wl" -a "$wl" $child \
        -- -q -f -rs="$seed" -evict="$pol" run "$wl" 2>&1)
  exit_s=$(echo "$raw" | grep -oE "$wl: exit\([-0-9]+\)" | tail -1 | grep -oE '[-0-9]+' | tail -1)
  [ -z "$exit_s" ] && exit_s=null
  ticks=$(echo "$raw" | sed -n 's/^Timer: \([0-9]*\) ticks$/\1/p' | tail -1)
  [ -z "$ticks" ] && ticks=null
  # Turn "KSTAT a=1 b=2 ..." into "\"a\":1,\"b\":2,...".
  ks=$(echo "$raw" | grep -m1 '^KSTAT ' \
        | sed -e 's/^KSTAT //' \
        | tr ' ' '\n' | sed -n 's/^\([a-z_]*\)=\([0-9]*\)$/"\1":\2/p' | paste -sd,)
  [ -z "$ks" ] && ks='"evictions":null'
  printf '{"policy":"%s","workload":"%s","mem_mb":%s,"seed":%s,"exit":%s,"ticks":%s,%s}\n' \
    "$pol" "$wl" "$mem" "$seed" "$exit_s" "$ticks" "$ks" >>"$out"
  echo "  $wl pol=$pol m=$mem rs=$seed -> exit=$exit_s ticks=$ticks $(echo "$raw" | grep -oE 'evictions=[0-9]+')"
}

# Axis 1 (headline): policy comparison at the heavy-eviction point (m=4),
# which every workload reaches reliably and the standard suite also uses.
echo "=== axis 1: policy comparison @ -m4"
for wl in page-parallel page-merge-par; do
  for pol in clock fifo random; do
    for s in $SEEDS; do run_one "$pol" "$wl" 4 "$s"; done
  done
done

# Axis 2 (secondary): paging curve -- clock only, growing RAM, to show
# evictions fall toward zero as the working set starts to fit.
echo "=== axis 2: paging curve (clock)"
for wl in page-parallel page-merge-par; do
  for mem in 4 5 6; do
    for s in $SEEDS; do run_one clock "$wl" "$mem" "$s"; done
  done
done

echo "=== done: $(wc -l <"$out") runs -> $out"
