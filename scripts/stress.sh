#!/usr/bin/env bash
# Runs a test suite many times, with varying parallelism, and reports
# any test that ever failed.  Proves the suites are not flaky (claim
# C6).  Run inside the Docker image.
#
#   scripts/stress.sh [-n RUNS] SUITE [SUITE ...]
set -uo pipefail

runs=20
while getopts "n:" o; do case $o in
  n) runs=$OPTARG ;; *) echo "usage: $0 [-n RUNS] SUITE..." >&2; exit 2 ;;
esac; done
shift $((OPTIND - 1))
[ "$#" -ge 1 ] || { echo "usage: $0 [-n RUNS] SUITE..." >&2; exit 2; }

src="${PINTOS_SRC:-/pintos-src}"
declare -A fail_count
total_fail=0 r=0

for suite in "$@"; do
  for ((r = 1; r <= runs; r++))
    do
      jobs=$(( (r % 4) + 1 ))        # vary -j across 1..4 to shake out races
      make -s -C "$src/$suite" >/dev/null 2>&1
      make -s -C "$src/$suite/build" -k -j"$jobs" check >/tmp/stress.$$ 2>&1 || true
      while read -r t; do
        fail_count["$suite/$t"]=$(( ${fail_count["$suite/$t"]:-0} + 1 ))
        total_fail=$((total_fail + 1))
      done < <(grep '^FAIL ' /tmp/stress.$$ | awk '{print $2}')
      printf 'stress %s run %d/%d (-j%d): %s\n' "$suite" "$r" "$runs" "$jobs" \
        "$(grep -c '^pass ' /tmp/stress.$$) pass, $(grep -c '^FAIL ' /tmp/stress.$$) fail"
    done
done
rm -f /tmp/stress.$$

echo "----"
if [ "$total_fail" -eq 0 ]; then
  echo "stress: PASS ($runs runs/suite, 0 flaky failures)"
else
  echo "stress: $total_fail failures across runs:"
  for k in "${!fail_count[@]}"; do echo "  $k: ${fail_count[$k]}"; done
fi
exit $((total_fail == 0 ? 0 : 1))
