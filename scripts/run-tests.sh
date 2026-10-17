#!/usr/bin/env bash
# Build each Pintos project and run its full test suite (`make check`).
# Usage: run-tests.sh [project ...]   (default: every project directory present)
# Writes raw output and a summary to $RESULTS_DIR/<project>/ (default /results).
set -u

SRC="${PINTOS_SRC:-/pintos-src}"
RESULTS_DIR="${RESULTS_DIR:-/results}"
JOBS="${JOBS:-$(nproc)}"

if [ "$#" -gt 0 ]; then
  projects=("$@")
else
  projects=()
  for p in threads userprog vm filesys; do
    [ -f "$SRC/$p/Make.vars" ] && projects+=("$p")
  done
  if [ "${#projects[@]}" -eq 0 ]; then
    echo "No Pintos projects found under $SRC (expected threads/, userprog/, vm/ or filesys/)." >&2
    exit 2
  fi
fi

status=0
for p in "${projects[@]}"; do
  out="$RESULTS_DIR/$p"
  mkdir -p "$out"
  echo "=== $p: build"
  make -C "$SRC/$p" clean >/dev/null 2>&1
  if ! make -C "$SRC/$p" >"$out/build.log" 2>&1; then
    echo "$p: BUILD FAILED (see $out/build.log)" | tee "$out/summary.txt"
    status=1
    continue
  fi

  # -k: one test whose run errors out (e.g. a filesys persistence step) must
  # not stop the rest of the suite.
  echo "=== $p: make -k check (-j$JOBS)"
  start=$(date +%s)
  make -C "$SRC/$p/build" -k -j"$JOBS" check ${SIMULATOR:+SIMULATOR=$SIMULATOR} \
    >"$out/check.log" 2>&1
  end=$(date +%s)

  # Keep each test's raw output and verdict.
  (cd "$SRC/$p/build" && find tests -name '*.output' -o -name '*.result' -o -name '*.errors' \
     | tar -cf - -T -) | tar -xf - -C "$out" 2>/dev/null
  [ -f "$SRC/$p/build/results" ] && cp "$SRC/$p/build/results" "$out/results"
  make -C "$SRC/$p/build" grade >"$out/grade.log" 2>&1 || true
  [ -f "$SRC/$p/build/grade" ] && cp "$SRC/$p/build/grade" "$out/grade"

  # Verdicts come from each test's .result file (first line PASS/FAIL), so
  # they are counted even when make stopped before printing its summary.
  make -C "$SRC/$p/build" -s --eval 'print-tests: ; @echo $(TESTS)' print-tests \
    2>/dev/null | tr ' ' '\n' | sed '/^$/d' | sort >"$out/expected-tests.txt"
  expected=$(wc -l <"$out/expected-tests.txt")
  : >"$out/verdicts.txt"
  find "$out/tests" -name '*.result' 2>/dev/null | sort | while read -r r; do
    t=${r#"$out/"}; t=${t%.result}
    echo "$(head -1 "$r") $t" >>"$out/verdicts.txt"
  done
  pass=$(grep -c '^PASS ' "$out/verdicts.txt")
  fail=$(grep -c '^FAIL ' "$out/verdicts.txt")
  {
    echo "project:  $p"
    echo "passed:   $pass / $((pass + fail))"
    [ "$expected" -gt 0 ] && [ "$expected" -ne $((pass + fail)) ] \
      && echo "WARNING:  $expected tests expected, $((pass + fail)) produced a verdict"
    echo "runtime:  $((end - start)) s (make check, -j$JOBS, ${SIMULATOR:-default simulator})"
    comm -23 "$out/expected-tests.txt" <(awk '{print $2}' "$out/verdicts.txt" | sort) \
      | sed 's/^/  no verdict: /'
    echo "failing:"
    grep '^FAIL ' "$out/verdicts.txt" | sed 's/^FAIL /  /'
  } | tee "$out/summary.txt"
  [ "$fail" -eq 0 ] || status=1
done
exit $status
