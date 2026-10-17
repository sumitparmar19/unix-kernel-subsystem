#!/usr/bin/env bash
# Build the Docker test image from this checkout and run Pintos test suites.
#
#   scripts/test.sh [-l LABEL] [-j JOBS] SUITE...     SUITE: threads userprog vm ipc limits
#
# Results go to analysis/test-results/LABEL/<suite>/ (default LABEL: latest):
# summary.txt, verdicts.txt, results.json, check.log and per-test outputs.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
label=latest
jobs=$(nproc)
while getopts "l:j:" opt; do
  case $opt in
    l) label=$OPTARG ;;
    j) jobs=$OPTARG ;;
    *) echo "usage: $0 [-l LABEL] [-j JOBS] SUITE..." >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))

image=${PINTOS_IMAGE:-pintos-kernel}
docker build -q -t "$image" ${DOCKER_BUILD_ARGS:-} "$root" >/dev/null
out="$root/analysis/test-results/$label"
mkdir -p "$out"
docker run --rm -e JOBS="$jobs" -v "$out:/results" "$image" "$@"
