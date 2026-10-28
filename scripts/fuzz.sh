#!/usr/bin/env bash
# Run the system-call fuzzer in Pintos and report whether the kernel
# survived (no panic, no hang, children all exit cleanly, file ops still
# work afterwards).  Used in CI.
#
#   scripts/fuzz.sh [-k userprog|ipc] [-s SEED0] [-n NSEEDS] [-i ITERS] [-t SECS]
#
# Must be run inside the Docker image (pintos on PATH), e.g. via
#   docker run --rm pintos-kernel ... or scripts/in-docker.sh.
set -euo pipefail

proj=userprog seed0=1 nseeds=10 iters=2000 timeout=300 vm=
while getopts "k:s:n:i:t:m" o; do case $o in
  k) proj=$OPTARG ;; s) seed0=$OPTARG ;; n) nseeds=$OPTARG ;;
  i) iters=$OPTARG ;; t) timeout=$OPTARG ;;
  m) vm=" v" ;;   # VM mode: also fuzz mmap/munmap (only on a VM kernel)
  *) echo "usage: $0 [-k proj] [-s seed0] [-n nseeds] [-i iters] [-t secs] [-m]" >&2; exit 2 ;;
esac; done

src="${PINTOS_SRC:-/pintos-src}"
build="$src/$proj/build"
make -s -C "$src/examples" fuzz >/dev/null
make -s -C "$src/$proj" >/dev/null

log=$(mktemp)
cmd="fuzz P $seed0 $nseeds $iters$vm"
echo "fuzz: $proj, seeds $seed0..$((seed0 + nseeds - 1)), $iters calls each${vm:+ (vm)}"
( cd "$build" && pintos -v -k -T "$timeout" --qemu --filesys-size=4 ${vm:+--swap-size=4} \
    -p ../../examples/fuzz -a fuzz -- -q -f run "$cmd" ) >"$log" 2>&1 || true

echo "---- kernel output ----"
grep -aE '^fuzz:|PANIC|TIMEOUT' "$log" | head -20 || true
echo "-----------------------"

fail=0
grep -qa 'fuzz: kernel healthy after fuzzing' "$log" || { echo "MISSING health line"; fail=1; }
if grep -qa 'PANIC' "$log"; then echo "KERNEL PANIC during fuzzing"; fail=1; fi
if grep -qa 'TIMEOUT' "$log"; then echo "KERNEL HANG (timeout) during fuzzing"; fail=1; fi
grep -qaE "fuzz: survivors $nseeds/$nseeds" "$log" || { echo "not all survivors finished (hang or corruption)"; fail=1; }
if grep -qa "leaked [1-9]" "$log"; then echo "a bad pointer slipped through"; fail=1; fi

if [ "$fail" -eq 0 ]; then
  grep -aoE "fuzz: [0-9]+ confirmed system calls[^\n]*" "$log" | head -1
  echo "fuzz: PASS"
else
  echo "fuzz: FAIL"
fi
rm -f "$log"
exit $fail
