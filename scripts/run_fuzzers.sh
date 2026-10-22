#!/usr/bin/env bash
# Run every libFuzzer target for a fixed time on a working copy of its seed corpus plus the
# saved regressions. Usage: scripts/run_fuzzers.sh <dir with fxval_fuzz_* binaries> [seconds]
# A crash, timeout or sanitizer report fails the script and leaves the input in
# <dir>/../fuzz-artifacts/<target>/ (the CI job uploads that folder).
set -euo pipefail

bin_dir=$(cd "${1:?directory with the fxval_fuzz_* binaries}" && pwd)
seconds=${2:-30}
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
artifacts="$bin_dir/../fuzz-artifacts"
mkdir -p "$artifacts"
trap 'rm -rf "$work"' EXIT

status=0
for target in vanilla barrier delta risk credit sha256 price_config; do
  exe="$bin_dir/fxval_fuzz_$target"
  if [[ ! -x "$exe" ]]; then
    echo "missing $exe" >&2
    status=1
    continue
  fi
  mkdir -p "$work/$target" "$artifacts/$target"
  cp -r "$root/tests/fuzz/corpus/$target/." "$work/$target/"
  if [[ -d "$root/tests/fuzz/regressions/$target" ]]; then
    cp -r "$root/tests/fuzz/regressions/$target/." "$work/$target/"
  fi
  echo "=== $target: ${seconds}s on $(find "$work/$target" -type f | wc -l) inputs"
  if ! "$exe" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 -max_len=256 \
      -artifact_prefix="$artifacts/$target/" "$work/$target"; then
    echo "FAILED: $target (input saved under $artifacts/$target/)" >&2
    status=1
  fi
done
exit $status
