#!/usr/bin/env bash
# Run the main checks locally, in the same order CI cares about.
#   scripts/check.sh          everything
#   scripts/check.sh quick    format, debug build and tests only
set -euo pipefail

cd "$(dirname "$0")/.."
mode="${1:-all}"

step() { printf '\n==> %s\n' "$*"; }

step "clang-format"
git ls-files '*.cpp' '*.hpp' 2>/dev/null | xargs -r clang-format --dry-run --Werror \
  || find include src tests examples \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
     | xargs -0 clang-format --dry-run --Werror

step "debug build and tests (contract checks on)"
cmake --preset debug >/dev/null
cmake --build --preset debug
ctest --preset debug

if [[ "$mode" == "quick" ]]; then
  echo "quick checks passed"
  exit 0
fi

step "release build and tests"
cmake --preset release >/dev/null
cmake --build --preset release
ctest --preset release

for preset in asan-ubsan tsan; do
  step "$preset"
  cmake --preset "$preset" >/dev/null
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

step "clang-tidy (library sources)"
clang-tidy -p build/debug --quiet src/*.cpp apps/lib/*.cpp apps/price/main.cpp

step "python: ruff, reference table, pytest (needs: pip install numpy scipy jsonschema pytest ruff)"
ruff check python
ruff format --check python
regen="$(mktemp)"
python3 python/gen_gk_reference.py "$regen"
python3 python/compare_csv.py "$regen" tests/validation/data/gk_reference.csv
rm -f "$regen"
pytest -q

echo
echo "all checks passed"
