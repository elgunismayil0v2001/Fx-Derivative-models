# Changelog

All notable changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added (week 5: QE scheme, variance reduction, Feller study)

- `mc::HestonQuadraticExponential`: Andersen's QE scheme with central discretisation and martingale
  correction, `heston_qe_mc_price`; `NormalStream` can be antithetic (sign flip).
- `mc/variance_reduction.hpp`: `run_antithetic`, `run_variance_reduced` (antithetic and/or control
  variate), `estimate_control_coefficient` (pilot run on separate streams); `McReduction` options for
  the vanilla wrappers, new overloads of `heston_euler_mc_price`.
- V-06 (Euler vs QE when Feller fails, Feller-ratio sweep), V-08 (variance ratios, unbiasedness, honest
  standard errors, a control with a wrong mean that must be rejected), QE path reference
  (`python/gen_qe_reference.py`, 160 rows) and its C++ test, unit tests for QE and variance reduction.
- `bench/bench_mc_scaling.cpp` (option `FXVAL_BUILD_BENCH`, off by default): thread scaling,
  scheme cost, work-normalised variance. Not part of `ctest`.
- ADR 0006.

### Changed (week 5)

- `mc/engine.hpp`: the body of `run_monte_carlo` moved into the new `run_samples` (path index in,
  sample out); `run_monte_carlo` calls it. No behaviour change: all week 4 tests pass unchanged,
  including the bitwise checks of V-07.

### Added (week 4: Monte Carlo engine)

- `fxval::mc`: Philox4x32-10 counter-based generator, `NormalStream` (Box-Muller, stream number =
  path number), `RunningStats` (Welford with exact merge), the `PathModel` concept and
  `run_monte_carlo` (threads, fixed blocks, merge in block order), exact `GbmModel`,
  `HestonEulerFullTruncation`, and `gbm_mc_price` / `heston_euler_mc_price` with standard errors.
- V-05 (Monte Carlo vs analytic prices, bias vs step size with a power check) and V-07 (bitwise
  identical results for any thread count, with a control that must fail).
- Independent Python reference for the random numbers and Heston paths
  (`python/gen_mc_reference.py`, 120-row table) and a C++ test against it.
- ADR 0005 (Monte Carlo engine), model documentation section 5. ADRs 0001 (error policy) and
  0003 (testing strategy), which other documents link to, are now in `docs/adr/`.

### Changed

- The library now links `Threads::Threads` (`std::jthread`), and `fxvalConfig.cmake` finds it.
- `.clang-tidy`: `clang-analyzer-core.uninitialized.Assign` is disabled. It reports a false
  positive on libstdc++ `std::variant` whenever an `Error` is copied from one `Result<T>` into
  another (reproduced on a 6-line file without project code).
- Restored `SpacesBeforeTrailingComments: 2` in `.clang-format` and the Python reference steps in
  CI, both lost in a merge; `build/release/` is no longer tracked (it contained absolute paths of
  one machine and broke builds elsewhere).

### Added

- Independent Python reference for Garman-Kohlhagen (`python/gen_gk_reference.py`, 616-row table)
  and a C++ test comparing against it; CI regenerates the table.
- V-01 on 8000 markets and V-02 on 120 points with 5-point stencils, recording worst errors.
- Unit tests for degenerate inputs and error paths. ADR 0003 (testing strategy), `docs/repo_guide.md`.

### Changed

- `garman_kohlhagen_greeks` returns `invalid_argument` when volatility or expiry is zero, instead
  of zeros (this also removes a wrong-sign put theta in the old zero-volatility branch).
- `.clang-format` now sets `SpacesBeforeTrailingComments: 2`, and all sources were reformatted so
  the CI format job passes. Build output and `.DS_Store` files are no longer tracked.

- Garman-Kohlhagen European FX price and Greeks, `Spot` unit, and ACT/365 fixed year fractions.
  Validation tests V-01 (put-call parity) and V-02 (Greeks vs finite differences).
- GitHub Actions CI, `.clang-format`, `.clang-tidy`, `.pre-commit-config.yaml`, and `.gitignore`.
- CMake build with presets (debug, release, asan-ubsan, tsan, coverage, release-vcpkg), install
  and export of `fxval::fxval`, and an external consumer example.
- `Result<T>` error type, contract checks, and typed units (`Volatility`, `Rate`, `Years`,
  `Strike`, `Notional`) with domain validation. ADR 0001 records the error policy.
- Unit tests and a test that keeps `docs/test_catalogue.md` consistent (V-01 to V-21 listed once).
- `python/data_download.py` for NBP, ECB and FRED data with provenance files, and offline tests.
- Documentation skeleton: model documentation draft (Garman-Kohlhagen derivation), test
  catalogue, architecture, ADR template, ADR 0001 and ADR 0002.
