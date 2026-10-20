# Changelog

All notable changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.7.0] - 2026-10-20

### Added (week 8: Heston calibration to smile quotes)

- `numerics/brent.hpp` (bracketed root finding), `numerics/least_squares.hpp` (Levenberg-Marquardt with
  Marquardt scaling and Nielsen damping, central-difference Jacobian, standard errors that are infinite when the
  data do not determine a parameter), `pricing/implied_volatility.hpp` (Garman-Kohlhagen implied volatility
  through the out-of-the-money option).
- `calibration/heston_smile.hpp`: Heston implied volatility and the delta quotes a Heston model implies.
- `calibration/heston_calibration.hpp`: `calibrate_heston` (bounds by logistic map, data-driven and
  counter-based random starts, per-start record, near-bound flags, standard errors). ADR 0009.
- Validation: V-11 (exact quotes on 24 sets, one-expiry identification control that must fail, noise study),
  CAL-REF (implied volatilities and delta quotes against independent Python),
  `python/gen_calibration_reference.py` and its pytest file; unit tests for every new module.
- Documentation: model documentation sections 4.6 to 4.11, validation report update (findings F-11 to F-13,
  limitations L-14 to L-16), mutation table for the new code.

### Fixed

- Found by CAL-REF: `heston_delta_quotes` searched the wing volatility from five times the model level, where
  a far wing option price underflows and the search failed on a 2-year, strongly skewed set. The upper end of
  the bracket is now found by scanning upwards.

## [0.6.0] - 2026-10-18

### Added (week 7: FX smile from delta quotes, arbitrage checks)

- `numerics/normal.hpp`: normal density, distribution function and quantile (Acklam plus one Halley
  step).
- `smile/`: delta convention (spot or forward delta, premium-excluded, delta-neutral ATM), quotes
  (`VolSpread`, `DeltaQuotes`), `SmileSlice` (quadratic in forward log-moneyness through the 25-delta put,
  ATM and 25-delta call), `VolSurface` (linear in total variance at fixed log-moneyness, no
  extrapolation), butterfly and calendar arbitrage reports. ADR 0008.
- Validation: V-09 (20,160 round-trip cases with a wrong-convention control), V-10 (density factor against
  option-price convexity, theory-based expectations, per-scenario report), SMILE-REF (256 rows against an
  independent Python implementation using root finding).
- `examples/smile/` runnable example, `configs/scenarios/smile_scenarios.csv` (synthetic, labelled),
  `python/plot_smiles.py`, figures in `docs/results/`.
- `tol::arbitrage_slack` in `core/limits.hpp`. Option `FXVAL_BUILD_EXAMPLES` (default on).
- Documentation: model documentation section 6, validation report update (findings F-08 to F-10,
  limitations L-09 to L-12), 25-change hand mutation table.

### Fixed

- `docs/test_catalogue.md`: the week-6 mutation table had cells where a phrase was repeated by mistake;
  rewritten.
- Removed `run.json` (output of a local run) from version control and ignored it.

## [0.5.0] - 2026-10-17

### Added (week 6: command-line program, run metadata, first validation report)

- `fxval-price` (`apps/`): prices European options with Garman-Kohlhagen, Heston semi-analytic, GBM or
  Heston Monte Carlo from a JSON configuration; JSON and CSV reports; exit codes 0 to 5; standard
  input and output; atomic report files. Reference: `docs/cli.md`, decision: ADR 0007.
- Strict, versioned configuration (`format_version` 1): unknown, repeated and missing keys, wrong
  types and out-of-domain values are errors that name the field path; the Monte Carlo seed is required.
  JSON Schema in `configs/schema/`; three example configurations in `configs/examples/`.
- Run metadata on every report: library version, git commit (recorded at build time, `-dirty` when
  tracked files changed), compiler, build type, SHA-256 of the input bytes, UTC timestamp
  (`SOURCE_DATE_EPOCH` honoured). `fxval/core/sha256.hpp`.
- Tests: 130 command-line-layer tests, regression tests with frozen results (checked against
  independent Python code), an allocation-counting test (zero allocations per simulated path), an
  end-to-end test of the real executable, SHA-256 vectors, 21 Python tests.
- `docs/validation_report.md`: first validation report (Garman-Kohlhagen, Heston, Monte Carlo
  schemes, variance reduction), with a findings register and a limitations list.
- Option `FXVAL_BUILD_APPS` (default ON); `vcpkg.json` lists `nlohmann-json`; version 0.5.0.

### Changed (week 6)

- The library `fxval` is unchanged except for the new `sha256` source; it still has no third-party
  dependency. The Docker runtime image now has `fxval-price` as its entry point.

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
