#!/usr/bin/env bash
# Runs a test suite many times to surface flaky (timing-dependent)
# failures (claim C6).  Builds each suite once, cleanly, then forces a
# real re-run of every test on each iteration by deleting the cached
# outputs.  Reports any test that ever failed.  Run in the Docker image.
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
total_fail=0

for suite in "$@"; do
  echo "stress $suite: clean build"
  make -s -C "$src/$suite" clean >/dev/null 2>&1
  if ! make -s -C "$src/$suite" >/tmp/build.$$ 2>&1; then
    echo "stress $suite: BUILD FAILED"; cat /tmp/build.$$; total_fail=$((total_fail+1)); continue
  fi
  bld="$src/$suite/build"
  for ((r = 1; r <= runs; r++))
    do
      # Force a genuine re-run: drop cached per-test results and the grade.
      find "$bld/tests" -name '*.output' -o -name '*.result' -o -name '*.errors' \
        2>/dev/null | xargs -r rm -f
      rm -f "$bld/results" "$bld/grade"
      jobs=$(( (r % 4) + 1 ))        # vary -j to shake out races
      make -s -C "$bld" -k -j"$jobs" check ${SIMULATOR:+SIMULATOR=$SIMULATOR} \
        >/tmp/stress.$$ 2>&1 || true
      p=$(grep -c '^pass ' /tmp/stress.$$); f=$(grep -c '^FAIL ' /tmp/stress.$$)
      while read -r t; do
        fail_count["$suite/$t"]=$(( ${fail_count["$suite/$t"]:-0} + 1 ))
        total_fail=$((total_fail + 1))
      done < <(grep '^FAIL ' /tmp/stress.$$ | awk '{print $2}')
      printf 'stress %s run %d/%d (-j%d): %d pass, %d fail\n' \
        "$suite" "$r" "$runs" "$jobs" "$p" "$f"
    done
done
rm -f /tmp/stress.$$ /tmp/build.$$

echo "----"
if [ "$total_fail" -eq 0 ]; then
  echo "stress: PASS ($runs runs/suite, 0 flaky failures)"
else
  echo "stress: $total_fail failures across runs:"
  for k in "${!fail_count[@]}"; do echo "  $k: ${fail_count[$k]} times"; done
fi
exit $((total_fail == 0 ? 0 : 1))
