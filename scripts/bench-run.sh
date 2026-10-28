#!/usr/bin/env bash
# Build the pintos-kernel image and run the page-replacement benchmark (scripts/bench.sh)
# inside it, writing analysis/benchmarks/bench.jsonl on the host.
#
#   scripts/bench-run.sh            # full matrix (SEEDS="1 2 3 4 5")
#   SEEDS="1 2" scripts/bench-run.sh
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
image=${PINTOS_IMAGE:-pintos-kernel}
docker build -q -t "$image" ${DOCKER_BUILD_ARGS:-} "$root" >/dev/null
out="$root/analysis/benchmarks"
mkdir -p "$out"
docker run --rm --entrypoint bash \
  -e SEEDS="${SEEDS:-1 2 3 4 5}" \
  -e RESULTS_DIR=/results \
  -v "$root/src:/pintos-src" -v "$out:/results" -v "$root/scripts:/bench-scripts:ro" \
  "$image" /bench-scripts/bench.sh 2>&1 | tee "$out/bench.log"
echo "wrote $out/bench.jsonl"
