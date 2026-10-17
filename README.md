# fx-model-validation

A C++20 library that prices FX options and measures FX portfolio market and counterparty risk,
together with an **independent validation** of those models: exact benchmarks, numerical tests,
model comparison, and a written validation report.

> **Status: week 6 of a 14-week plan (release v0.5).** The engineering foundation is in place, the
> Garman-Kohlhagen benchmark is checked by V-01 and V-02, the Heston semi-analytic pricer by V-03,
> V-04 and an independent Python table, and the multithreaded Monte Carlo engine (Euler full
> truncation and Andersen's QE scheme, antithetic and control variates) by V-05 to V-08 and
> independent path tables, and by a command-line program `fxval-price` whose reports record the
> build, the input hash and the seed. A first validation report is in `docs/validation_report.md`.
> Smile, calibration, risk and CVA are not started.

## What exists today

| Area | State |
|---|---|
| Build (CMake presets, install and export, `find_package(fxval)`) | working |
| Error policy (`Result<T>`, contract checks) and typed units (`Spot`, `Volatility`, `Rate`, `Years`, `Strike`, `Notional`) | working, tested |
| Garman-Kohlhagen price and Greeks, ACT/365 fixed | working; V-01 and V-02 passing |
| Heston semi-analytic European pricer (stable characteristic function, Lewis integral) | working; V-03, V-04 and reference table (589 rows) passing |
| Monte Carlo engine: counter-based Philox random numbers, exact GBM, Heston Euler with full truncation, Heston QE, antithetic and control variates, results independent of the thread count | working; V-05 to V-08 and two path tables (120 and 160 rows) passing |
| `fxval-price` command-line program: validated, versioned JSON configuration; JSON and CSV reports with run metadata (version, git commit, config SHA-256, seed, time); documented exit codes | working; see `docs/cli.md` |
| Unit, validation, regression, allocation and end-to-end tests, independent Python references, catalogue consistency test | 280 tests in `ctest`, 59 Python tests |
| Scaling benchmark (`bench/`, off by default) | measured on one machine, see model documentation 5.10 |
| CI (format, four compilers, sanitizers, clang-tidy, coverage, install check, Python, Docker) | `.github/workflows/ci.yml` |
| Data download scripts (NBP, ECB, FRED) with provenance files | parsers tested offline |
| Smile, calibration, risk, CVA | not started (see `docs/test_catalogue.md`) |

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
find_package(fxval 0.5 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE fxval::fxval)
```

`examples/consumer/` is a complete example, built against an installed copy in CI.

## Command-line program

```sh
build/release/apps/fxval-price --config configs/examples/heston_mc_qe.json --output run.json
```

Prices European options with Garman-Kohlhagen, the Heston semi-analytic pricer or Monte Carlo (Euler,
QE, antithetic and control variates) from a strict, versioned JSON file. The report (JSON or CSV)
records the library version, git commit, compiler, SHA-256 of the input, the seed and the time, so a
number can be traced to what produced it. Exit codes, the configuration format and the report
format are in `docs/cli.md`. The library itself does not depend on the JSON parser used by the
program; build without the program with `-DFXVAL_BUILD_APPS=OFF`.

## Data

`python/data_download.py` downloads free FX and rate history and writes a CSV plus a provenance
file. FX option quotes are paid data, so smile inputs will be **synthetic and labelled as such**.
See `docs/model_documentation.md`, section 11.

## Documentation

* `docs/repo_guide.md`: what every folder and file is for
* `docs/model_documentation.md`: model specification and derivations (draft)
* `docs/test_catalogue.md`: the 21 planned validation tests (V-01 to V-21) and their status
* `docs/validation_report.md`: the validation report (draft for v0.5: Garman-Kohlhagen, Heston, Monte Carlo)
* `docs/cli.md`: the `fxval-price` program, its configuration, report and exit codes
* `docs/architecture.md`: layers and rules
* `docs/adr/`: architecture decision records
* `ROADMAP.md`, `CHANGELOG.md`, `CONTRIBUTING.md`

## Limitations

* Implemented: Garman-Kohlhagen, semi-analytic Heston, and Monte Carlo with the Euler and QE
  schemes. The Euler scheme is badly biased when the Feller condition fails; QE was measured on four
  Feller-violating parameter sets only (sections 5.8 and 5.9 of the model documentation). Do not read
  later roadmap items as results.
* The program prices one model per file and only European options; Heston has no Greeks yet. The
  GitHub Actions workflows, Docker and vcpkg builds have not been run (validation report, L-05).
* Interest rates will be deterministic in v1.0, and default probabilities in CVA are assumptions.

## Licence

MIT, see `LICENSE`.
