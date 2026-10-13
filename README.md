# fx-model-validation

A C++20 library that prices FX options and measures FX portfolio market and counterparty risk,
together with an **independent validation** of those models: exact benchmarks, numerical tests,
model comparison, and a written validation report.

> **Status: week 3 of a 14-week plan.** The engineering foundation is in place, the
> Garman-Kohlhagen benchmark is checked by V-01 and V-02, and the Heston semi-analytic pricer is
> checked by V-03, V-04 and an independent Python table. Monte Carlo, smile, calibration, risk and
> CVA are not started.

## What exists today

| Area | State |
|---|---|
| Build (CMake presets, install and export, `find_package(fxval)`) | working |
| Error policy (`Result<T>`, contract checks) and typed units (`Spot`, `Volatility`, `Rate`, `Years`, `Strike`, `Notional`) | working, tested |
| Garman-Kohlhagen price and Greeks, ACT/365 fixed | working; V-01 and V-02 passing |
| Heston semi-analytic European pricer (stable characteristic function, Lewis integral) | working; V-03, V-04 and reference table (589 rows) passing |
| Unit tests, V-01 to V-04, independent Python references, catalogue consistency test | 72 tests in `ctest` |
| CI (format, four compilers, sanitizers, clang-tidy, coverage, install check, Python, Docker) | `.github/workflows/ci.yml` |
| Data download scripts (NBP, ECB, FRED) with provenance files | parsers tested offline |
| Monte Carlo, smile, calibration, risk, CVA | not started (see `docs/test_catalogue.md`) |

## Build and test

Requirements: a C++20 compiler (GCC 13+ or Clang 17+), CMake 3.25+, Ninja. GoogleTest is
downloaded automatically when it is not installed.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

| Preset | Purpose |
|---|---|
| `debug` | contract checks on |
| `release` | optimised, warnings as errors |
| `asan-ubsan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | ThreadSanitizer |
| `coverage` | line coverage with gcov |
| `release-vcpkg` | dependencies from vcpkg (needs `VCPKG_ROOT`) |

`scripts/check.sh` runs the main checks in one go. A Docker build runs the tests inside the image:
`docker build -t fxval .`

## Use as a library

```cmake
find_package(fxval 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE fxval::fxval)
```

`examples/consumer/` is a complete example, built against an installed copy in CI.

## Data

`python/data_download.py` downloads free FX and rate history and writes a CSV plus a provenance
file. FX option quotes are paid data, so smile inputs will be **synthetic and labelled as such**.
See `docs/model_documentation.md`, section 11.

## Documentation

* `docs/repo_guide.md`: what every folder and file is for
* `docs/model_documentation.md`: model specification and derivations (draft)
* `docs/test_catalogue.md`: the 21 planned validation tests (V-01 to V-21) and their status
* `docs/architecture.md`: layers and rules
* `docs/adr/`: architecture decision records
* `ROADMAP.md`, `CHANGELOG.md`, `CONTRIBUTING.md`

## Limitations

* Only the Garman-Kohlhagen closed form is implemented. Do not read later roadmap items as results.
* Interest rates will be deterministic in v1.0, and default probabilities in CVA are assumptions.

## Licence

MIT, see `LICENSE`.
