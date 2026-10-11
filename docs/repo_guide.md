# Repository guide

What every folder and file is for. Status: week 2.

## The two kinds of test, in one paragraph

`tests/unit/` protects the code (known values, degenerate inputs, error paths).
`tests/validation/` gathers evidence about the models (parity, finite differences, an independent
Python implementation). Both are needed: unit tests make the library production quality,
validation tests are what a model-validation team would ask for. Reasoning:
`docs/adr/0003-testing-strategy.md`.

## Top level

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Defines the library `fxval` (sources listed explicitly), include paths, warnings, install and export rules. Adds `tests/`. |
| `CMakePresets.json` | Named build configurations: `debug`, `release`, `asan-ubsan`, `tsan`, `coverage`, `release-vcpkg`. |
| `vcpkg.json` | Dependency manifest for the vcpkg preset. |
| `Dockerfile` | Builds and tests the library in a clean container. |
| `pyproject.toml` | Python tool configuration (pytest, ruff) and optional dependencies for the reference generator. |
| `.clang-format`, `.clang-tidy`, `.pre-commit-config.yaml` | Code style, static analysis, checks before each commit. |
| `.gitignore` | Keeps `build/`, caches and `.DS_Store` out of git. |
| `README.md`, `CHANGELOG.md`, `ROADMAP.md`, `CONTRIBUTING.md`, `LICENSE` | Front page, history, plan, working rules, MIT licence. |

## `include/fxval/` and `src/`

| Header | Source | Purpose |
|---|---|---|
| `core/error.hpp` | `error.cpp` | `ErrorCode`, `Error`, `Result<T>`: how failures are reported (ADR 0001). |
| `core/contracts.hpp` | `contracts.cpp` | `FXVAL_EXPECTS`: programmer-mistake checks, Debug only. |
| `core/units.hpp` | `units.cpp` | Validated types `Spot`, `Strike`, `Rate`, `Volatility`, `Years`, `Notional`. An invalid raw `double` cannot reach a model. |
| `core/limits.hpp` | none | Domain bounds and the named test tolerances (`tol::put_call_parity`, ...). |
| `core/daycount.hpp` | `daycount.cpp` | ACT/365 fixed year fraction between two dates. |
| `pricing/garman_kohlhagen.hpp` | `garman_kohlhagen.cpp` | `garman_kohlhagen_price` and `garman_kohlhagen_greeks`: the benchmark model. |
| `version.hpp.in` | none | Template turned into the version header by CMake. |

## `tests/`

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Builds two executables, `fxval_unit_tests` and `fxval_validation_tests`, and registers every test with CTest. |
| `support/gk_test_support.hpp` | `must()` (unwrap a `Result` or fail with its message), `make_market()`, `gk_value()`, `gk_all()`. |
| `unit/test_error.cpp`, `test_units.cpp`, `test_version.cpp`, `test_daycount.cpp` | Error type, typed units, version, day count. |
| `unit/test_garman_kohlhagen.cpp` | Intrinsic value at expiry, zero volatility, parity at one point, delta relations. |
| `unit/test_garman_kohlhagen_errors.cpp` | Textbook values, currency-inversion symmetry, degenerate inputs, every error path. |
| `validation/test_v01_put_call_parity.cpp` | V-01 on a grid of 8000 markets; records the worst error. |
| `validation/test_v02_greeks_fd.cpp` | V-02: all Greeks against finite differences of the price on 120 points; records the worst error per Greek and where. |
| `validation/test_gk_reference.cpp` | C++ against the independent Python table (616 rows). |
| `validation/test_catalogue_consistency.cpp` | Fails if `docs/test_catalogue.md` lists a V-ID twice, never, or outside V-01..V-21. |
| `validation/data/gk_reference.csv` | The independent reference table, written by `python/gen_gk_reference.py`. |
| `regression/` | Empty until week 6. |

## `python/`

| File | Purpose |
|---|---|
| `data_download.py` | Downloads NBP, ECB and FRED history; writes CSV plus provenance file. |
| `gen_gk_reference.py` | Writes the reference table with SciPy, in a different algebraic form from the C++ code, fixed seed. |
| `tests/test_data_download.py`, `tests/test_gen_gk_reference.py` | Offline tests of the parsers and of the generator. |

## Other

| Path | Purpose |
|---|---|
| `cmake/` | Warning flags, sanitizer options, template for `find_package(fxval)`. |
| `examples/consumer/` | A tiny external project that links the installed library. |
| `scripts/check.sh` | Runs format, builds, tests, sanitizers, clang-tidy and Python checks locally. |
| `.github/workflows/ci.yml` | The same checks in GitHub Actions, plus the compiler matrix, coverage and Docker. |
| `docs/` | Model documentation, test catalogue, architecture, ADRs, this guide. |
| `bench/`, `configs/`, `data/`, `notebooks/` | Placeholders, empty until used. |
