# Repository guide

What every folder and file is for. Status: release 1.0.

## The two kinds of test, in one paragraph

`tests/unit/` protects the code (known values, degenerate inputs, error paths).
`tests/validation/` gathers evidence about the models (parity, finite differences, an independent
Python implementation). Both are needed: unit tests make the library production quality,
validation tests are what a model-validation team would ask for. Reasoning:
`docs/adr/0003-testing-strategy.md`.

## Top level

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Defines the library `fxval` (sources listed explicitly; links `Threads::Threads`), include paths, warnings, install and export rules. Adds `tests/`. |
| `CMakePresets.json` | Named build configurations: `debug`, `release`, `asan-ubsan`, `tsan`, `coverage`, `release-vcpkg`. |
| `vcpkg.json` | Dependency manifest for the vcpkg preset. |
| `Dockerfile` | Builds and tests the library in a clean container; the runtime image has `fxval-price` as its entry point. |
| `apps/` | Command-line programs. `apps/lib/` is the static library `fxval_app` (configuration parser, runner, report writers, run metadata, logger, CLI); `apps/price/main.cpp` is ten lines. Not part of the public API. |
| `configs/examples/`, `configs/schema/` | Three example configurations (also the inputs of the regression tests) and the JSON Schema for editors and CI. |
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
| `models/heston.hpp` | `heston.cpp` | `HestonParams`, `HestonVanilla`, `log_forward_characteristic_function`, `heston_price`: semi-analytic Heston (docs section 4, ADR 0004). |
| `numerics/quadrature.hpp` | none (header-only template) | `adaptive_gauss_kronrod`: adaptive Gauss-Kronrod (7, 15) integration with an explicit `converged` flag. |
| `mc/philox.hpp` | none (header-only) | `Philox4x32`: counter-based random number generator, checked against the published known answers. |
| `mc/normal_stream.hpp` | none (header-only) | `NormalStream(seed, stream, antithetic = false)`: standard normals by Box-Muller from Philox blocks; stream number = path number. |
| `mc/running_stats.hpp` | none (header-only) | `RunningStats`: Welford mean and variance with an exact merge. |
| `mc/engine.hpp` | `mc_engine.cpp` | `McConfig`, `McEstimate`, the `PathModel` concept, `run_samples` (any sample function of the path index) and `run_monte_carlo`: threads, fixed blocks, merge in block order (V-07). `mc_engine.cpp` holds configuration validation. |
| `mc/gbm.hpp` | `mc_models.cpp` | `GbmModel`: exact GBM simulation (engine check). |
| `mc/heston_euler.hpp` | `mc_models.cpp` | `HestonEulerFullTruncation`: Euler scheme with full truncation; `simulate_terminal_state` for cross-checks. |
| `mc/heston_qe.hpp` | `mc_models.cpp` | `HestonQuadraticExponential`: Andersen QE scheme with martingale correction; `next_variance` exposes the variance step for tests. |
| `mc/variance_reduction.hpp` | none (header-only) | `run_antithetic`, `run_variance_reduced`, `estimate_control_coefficient`, `NoControl`: antithetic and control variates on top of `run_samples`. |
| `mc/vanilla.hpp` | `mc_vanilla.cpp` | `gbm_mc_price`, `heston_euler_mc_price`, `heston_qe_mc_price` (and `McReduction` options): discounted vanilla price with standard error. |
| `numerics/normal.hpp` | `normal.cpp` | `normal_pdf`, `normal_cdf`, `normal_quantile` (Acklam plus a Halley step). |
| `smile/delta_convention.hpp` | `delta_convention.cpp` | `SmileMarket`, `strike_from_delta`, `atm_strike`, `delta_at_strike`: the one delta convention implemented (docs 6.2). |
| `smile/quotes.hpp` | `quotes.cpp` | `VolSpread`, `DeltaQuotes`, `pivot_volatilities`: ATM, risk reversal, butterfly to the three pivot volatilities. |
| `smile/smile_slice.hpp` | `smile_slice.cpp` | `SmileSlice`: the quadratic through the 25-delta put, ATM and 25-delta call, with analytic derivatives. |
| `smile/vol_surface.hpp` | `vol_surface.cpp` | `VolSurface`: several smiles, linear in total variance at fixed log-moneyness, no extrapolation. |
| `smile/arbitrage_checks.hpp` | `arbitrage_checks.cpp` | `check_butterfly`, `check_calendar`, `check_surface`, `density_factor`: reports, never repairs. |
| `numerics/brent.hpp` | none (header-only template) | `brent_root`: bracketed root finding; used for implied volatility and the delta-strike fixed points. |
| `numerics/least_squares.hpp` | `least_squares.cpp` | `levenberg_marquardt`, `finite_difference_jacobian`, `parameter_standard_errors`: small dense least squares with an explicit termination reason. |
| `pricing/implied_volatility.hpp` | `implied_volatility.cpp` | `garman_kohlhagen_implied_volatility`: price to volatility through the out-of-the-money option. |
| `calibration/heston_smile.hpp` | `heston_smile.cpp` | `heston_implied_volatility`, `heston_delta_quotes`: the Heston model in market units (docs 4.6). |
| `calibration/heston_calibration.hpp` | `heston_calibration.cpp` | `calibrate_heston`: bounded multi-start least squares on volatility errors; reports every start, near-bound flags and standard errors (docs 4.7 to 4.9). |
| `pricing/barrier_closed_form.hpp` | `barrier_closed_form.cpp` | Contract types (`BarrierVanilla`, `TouchContract`, `DoubleNoTouchContract`, `FxMarket`) and the continuous-monitoring closed forms (docs 8.1). |
| `mc/barrier_mc.hpp` | `mc_barrier.cpp` | `gbm_barrier_mc_price`, `heston_barrier_mc_price` with discrete or Brownian-bridge monitoring (docs 8.2, 8.3). |
| `exotics/vanna_volga.hpp` | `vanna_volga.cpp` | `VannaVolga`: three variants, flat-volatility prices and Greeks (docs 7). |
| `exotics/barrier_comparison.hpp` | `barrier_comparison.cpp` | `compare_barrier_models`: GK flat, Vanna-Volga, Heston and the model-risk spread (docs 7.5). |
| `numerics/nelder_mead.hpp` | `nelder_mead.cpp` | `nelder_mead`: simplex minimiser with restarts; an objective error counts as +infinity. |
| `timeseries/volatility_models.hpp` | `volatility_models.cpp` | `ewma_variance`, `fit_garch` (GARCH and GJR, constrained by a softmax map, Hessian standard errors), `standardised_residuals` (docs 9.2, 9.3). |
| `risk/var.hpp` | `var.cpp` | `historical_var_es`, `normal_var_es`, `cornish_fisher_var` (docs 9.4). |
| `risk/portfolio.hpp` | `portfolio_risk.cpp` | `FxPortfolio`, delta-normal, delta-gamma, full-revaluation, historical and filtered-historical VaR (docs 9.5). |
| `risk/backtest.hpp` | `backtest.cpp` | `evaluate_exceptions` (Kupiec, independence, conditional coverage, traffic light) and `rolling_backtest` (docs 9.6). |
| `credit/exposure.hpp` | `exposure.cpp` | Exposure simulation: `simulate_spot_paths`, `simulate_netting_set_values`, `simulate_forward_values`, `simulate_option_values`, `exposure_profile` (docs 10.2). |
| `credit/cva.hpp` | `cva.cpp` | `cva_weights`, `cva_from_profile`, `cva_estimate`, `cva_difference` (docs 10.3). |
| `version.hpp.in` | none | Template turned into the version header by CMake. |

## `tests/`

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Builds two executables, `fxval_unit_tests` and `fxval_validation_tests`, and registers every test with CTest. |
| `support/heston_test_support.hpp` | `make_params()`, `make_heston_market()`, `heston_value()`, `gk_call()`, and a bisection implied-volatility helper for the smile test. |
| `support/gk_test_support.hpp` | `must()` (unwrap a `Result` or fail with its message), `make_market()`, `gk_value()`, `gk_all()`. |
| `unit/test_error.cpp`, `test_units.cpp`, `test_version.cpp`, `test_daycount.cpp` | Error type, typed units, version, day count. |
| `unit/test_garman_kohlhagen.cpp` | Intrinsic value at expiry, zero volatility, parity at one point, delta relations. |
| `unit/test_garman_kohlhagen_errors.cpp` | Textbook values, currency-inversion symmetry, degenerate inputs, every error path. |
| `validation/test_v01_put_call_parity.cpp` | V-01 on a grid of 8000 markets; records the worst error. |
| `validation/test_v02_greeks_fd.cpp` | V-02: all Greeks against finite differences of the price on 120 points; records the worst error per Greek and where. |
| `unit/test_nelder_mead.cpp`, `unit/test_volatility_models.cpp`, `unit/test_var_and_backtest.cpp` | Hand-computed values, limits, orderings and error paths of the market-risk code. |
| `validation/test_v16_garch_estimation.cpp`, `test_risk_reference.cpp`, `test_v17_var_backtests.cpp`, `test_v18_delta_normal_vs_revaluation.cpp`, `test_v21_lookahead.cpp` | V-16, RISK-REF, V-17, V-18, V-21. `support/risk_test_support.hpp` has the shared forecasters and simulators. |
| `unit/test_exposure_and_cva.cpp` | Exposure paths, netting, profile and CVA by hand, structural properties and error paths. |
| `validation/test_credit_reference.cpp`, `test_v19_forward_exposure.cpp`, `test_v20_cva_nested.cpp` | CREDIT-REF, V-19, V-20. `support/credit_test_support.hpp` has the market and trade builders. |
| `fuzz/fuzz_targets.hpp`, `fuzz/fuzz_config_target.hpp` | The seven fuzz targets as functions from bytes to a verdict (an empty string, or the violated invariant). |
| `fuzz/fuzz_<target>.cpp` | libFuzzer entry points (`LLVMFuzzerTestOneInput`), built with `-DFXVAL_BUILD_FUZZERS=ON` under clang. |
| `fuzz/test_fuzz_replay.cpp` | `fxval_fuzz_replay_tests`: seeds, regressions, seeded mutations and random inputs through every target, in every build. |
| `fuzz/corpus/`, `fuzz/regressions/`, `fuzz/make_seeds.py` | Seed inputs (generated deterministically), inputs that once failed (replayed forever) and the seed generator. See `docs/fuzzing.md`. |
| `unit/test_quadrature.cpp` | The integrator on integrals with known answers: polynomials, sine, Gaussian, sqrt singularity, narrow peak, reversed interval, non-convergence, rounding-level stop rule. |
| `unit/test_heston.cpp` | Parameter validation, characteristic-function identities (`phi(0) = phi(-i) = 1`, boundedness, first moment), `xi = 0` equals Garman-Kohlhagen, parity, bounds, convexity, smile sign, error paths. |
| `validation/test_v03_heston_vs_gk.cpp` | V-03: Heston to Garman-Kohlhagen as vol of vol goes to 0, checked at the theoretical order of convergence. |
| `validation/test_v04_heston_robustness.cpp` | V-04: no branch-cut jumps in maturity (with the 1993 formulation as a control that must fail), and 576 extreme parameter sets priced and checked for bounds, monotonicity and convexity. |
| `validation/test_heston_reference.cpp` | C++ Heston against the independent Python table (589 rows). |
| `validation/test_gk_reference.cpp` | C++ against the independent Python table (616 rows). |
| `support/mc_test_support.hpp` | `scaled_paths()` (fewer paths in Debug and sanitizer builds), `make_config()`, `bits_of()` (compare doubles bit for bit). |
| `unit/test_philox.cpp` | Philox known-answer vectors (also at compile time), avalanche, uniform mapping, stream determinism, moments, tail frequencies, Kolmogorov-Smirnov, independence between streams. |
| `unit/test_running_stats.cpp` | Welford against two-pass, huge-mean stability (with the textbook formula failing as a contrast), merge equals one pass. |
| `unit/test_mc_engine.cpp` | Engine: constants, steps passed through, path = stream, partial blocks, every configuration error, non-finite result; models: zero expiry, martingale property, constant-variance law, truncation reached. |
| `validation/test_v05_heston_mc_vs_analytic.cpp` | V-05: GBM engine vs Garman-Kohlhagen; Heston Euler vs the semi-analytic price; bias vs step size with a power check. |
| `validation/test_v07_mc_reproducibility.cpp` | V-07: bitwise equality over thread counts and repeats, with a naive-partition control that must differ. |
| `unit/test_heston_qe.cpp` | QE: argument checks, exact conditional mean and variance of the variance step in both branches, point mass at zero, positivity, martingale at few steps, determinism, one block per step. |
| `unit/test_variance_reduction.cpp` | Antithetic stream is the exact negation; exact identities (odd function cancels, perfect control, known coefficient); pilot streams; errors; bitwise equality over thread counts; wrapper errors. |
| `validation/test_v06_feller_study.cpp` | V-06: Euler vs QE bias over four Feller-violating sets, steps, strikes; martingale; switching level; Feller-ratio sweep. |
| `validation/test_v08_variance_reduction.cpp` | V-08: unbiasedness, wrong-mean control rejected, standard error vs spread over 80 seeds, variance ratios, GK control under GBM. |
| `validation/test_mc_qe_path_reference.cpp` | QE terminal states against the independent Python table (160 rows). |
| `validation/data/mc_qe_path_reference.csv` | The QE reference table, written by `python/gen_qe_reference.py`. |
| `validation/test_mc_path_reference.cpp` | Normals and Heston paths against the independent Python table (120 rows). |
| `validation/data/mc_path_reference.csv` | The path reference table, written by `python/gen_mc_reference.py`. |
| `support/smile_test_support.hpp` | `make_smile_market()`, `make_quotes()`, `make_slice()` and a reader for `configs/scenarios/smile_scenarios.csv`. |
| `unit/test_normal.cpp`, `test_delta_convention.cpp`, `test_quotes.cpp`, `test_smile_slice.cpp`, `test_vol_surface.cpp`, `test_arbitrage_checks.cpp` | The smile code: quantile known values and round trip, strikes by hand and against Garman-Kohlhagen Greeks, quote definitions and errors, pivots, derivatives, surface interpolation, check reports. |
| `validation/test_v09_delta_roundtrip.cpp` | V-09: 20,160 quote to strike to quote round trips, with a wrong-convention control. |
| `validation/test_v10_smile_arbitrage.cpp` | V-10: density factor against option-price convexity, theory-based verdicts, per-scenario report, frozen observed counts. |
| `validation/test_smile_reference.cpp` | C++ smile against the independent Python table (256 rows). |
| `validation/data/smile_reference.csv` | The smile reference table, written by `python/gen_smile_reference.py`. |
| `support/barrier_test_support.hpp` | Typed `FxMarket`, barrier levels and contracts from plain numbers. |
| `unit/test_barrier_closed_form.cpp`, `test_vanna_volga.cpp`, `test_barrier_mc.cpp` | The barrier and Vanna-Volga code: limits, parity, errors, weights, variants, reproducibility. |
| `validation/test_barrier_reference.cpp` | BARRIER-REF: 700 prices against independent Python integration. |
| `validation/test_v12_barrier_mc.cpp`, `test_v13_in_out_parity.cpp`, `test_v14_vanna_volga_pivots.cpp`, `test_v15_model_comparison.cpp` | V-12 to V-15. Criteria in the file headers. |
| `validation/data/garch_series.csv`, `garch_reference.csv` | The V-16 series and reference fits, written by `python/gen_garch_reference.py`. |
| `validation/data/risk_hits_reference.csv`, `risk_var_reference.csv` | RISK-REF tables, written by `python/gen_risk_reference.py`. |
| `validation/data/credit_reference.csv` | The CREDIT-REF and V-19 table, written by `python/gen_credit_reference.py`. |
| `validation/data/barrier_reference.csv` | The barrier reference table, written by `python/gen_barrier_reference.py`. |
| `support/calibration_test_support.hpp` | `make_calibration_market()`, `heston_slices()` (quotes a Heston model implies) and a small CSV reader for the calibration tables. |
| `unit/test_brent.cpp`, `test_implied_volatility.cpp`, `test_least_squares.cpp`, `test_heston_smile.cpp`, `test_heston_calibration.cpp` | The calibration code: root finder, implied volatility round trips and error paths, Levenberg-Marquardt on problems with known answers and its standard errors, the Heston model in market units, calibration (recovery, determinism, bounds, one expiry, currency-swap invariance). |
| `validation/test_calibration_reference.cpp` | CAL-REF: Heston implied volatility (160 rows) and delta quotes (120 rows) against independent Python. |
| `validation/test_v11_heston_calibration.cpp` | V-11: parameter recovery on 24 sets, one-expiry identification control, noise study. Criteria in the file header. |
| `validation/data/heston_iv_reference.csv`, `calibration_reference.csv` | The calibration reference tables, written by `python/gen_calibration_reference.py`. |
| `integration/run_smile_example.cmake` | End-to-end test of `fxval-example-smile` (exit code, files, row counts). |
| `validation/test_catalogue_consistency.cpp` | Fails if `docs/test_catalogue.md` lists a V-ID twice, never, or outside V-01..V-21. |
| `validation/data/heston_reference.csv` | The Heston reference table, written by `python/gen_heston_reference.py`. |
| `validation/data/gk_reference.csv` | The independent reference table, written by `python/gen_gk_reference.py`. |
| `regression/` | `test_price_regression.cpp` and `expected/*.results.json`: the example configurations priced and compared with frozen results (relative 1e-9). `FXVAL_UPDATE_GOLDEN=1` rewrites them. |
| `app/test_price_config.cpp` | Every rule of the configuration format (one case each, message must name the field), repeated keys, nesting, 100,001 options, and 24,000 randomly damaged documents. |
| `app/test_price_runner.cpp` | Runner agrees bit for bit with the library; put-call parity, order and thread-count invariance, notional scaling, failure isolation, missing Greeks. |
| `app/test_report.cpp` | JSON structure and field order, lossless numbers, error rows, CSV quoting. |
| `app/test_cli.cpp` | Argument handling, every exit code, stdin, file output, size limit, determinism, log versus report streams. |
| `app/test_run_metadata.cpp` | Timestamp formatter against Python datetime (60 instants), metadata record. |
| `app/test_allocation_free.cpp` | Counts heap allocations: zero per simulated path; engine allocations independent of path count; a model that allocates is detected. |
| `unit/test_sha256.cpp` | SHA-256 against NIST vectors, padding boundaries, one million bytes. |
| `integration/run_cli.cmake` | Runs the real `fxval-price`: exit codes, both formats, config hash against CMake's `file(SHA256)`, `SOURCE_DATE_EPOCH`. |

## `python/`

| File | Purpose |
|---|---|
| `data_download.py` | Downloads NBP, ECB and FRED history; writes CSV plus provenance file. |
| `gen_heston_reference.py` | Writes the Heston table with the textbook P1/P2 probabilities and SciPy QUADPACK (a different integral, parametrisation and integrator from the C++ code). |
| `gen_qe_reference.py` | Writes the QE path table: the scheme re-implemented in plain Python (same paper, separately written code; random numbers from `gen_mc_reference.py`). |
| `gen_mc_reference.py` | Writes the Monte Carlo path table: Philox, Box-Muller and the Heston Euler path re-implemented in plain Python (no shared code with C++). |
| `tests/test_price_examples.py` | Checks the example configurations against the JSON Schema (and that the schema rejects 14 damaged copies), and the frozen results against SciPy (Garman-Kohlhagen), the textbook Heston integrals and, for Monte Carlo, the independent price within 4 standard errors. |
| `gen_smile_reference.py` | Writes the smile table: strikes by root finding on the spot-form delta, smile by a Vandermonde solve (different methods from the C++ code). |
| `gen_garch_reference.py` | Writes the 12 simulated return series and the independent SLSQP fits and Hessian standard errors for V-16. |
| `gen_risk_reference.py` | Writes the 40 hit sequences and 186 VaR rows of RISK-REF. |
| `compare_with_arch.py` | Optional comparison with the `arch` package; exits quietly when it is missing. Never run in this project. |
| `build_traceability.py` | Builds the traceability matrix of the validation report (section 10) from the test catalogue and the findings table. |
| `gen_credit_reference.py` | Writes the 80-row credit table: forward EE and PFE (Black-76), option prices and exact CVA, CVA by intervals. |
| `gen_barrier_reference.py` | Writes the 700-row barrier table by numerical integration (killed densities by images, first-passage integral, sum of images). |
| `gen_calibration_reference.py` | Writes the two calibration tables with textbook Heston prices, `brentq` implied volatilities and nested `brentq` delta strikes. |
| `plot_smiles.py` | Draws `docs/results/smile_*.png` from the CSV files written by `fxval-example-smile` (needs matplotlib). |
| `compare_csv.py` | Compares two CSV tables, numbers to a tolerance and everything else exactly. CI and `scripts/check.sh` use it instead of `cmp` for the Garman-Kohlhagen table (tests: `tests/test_compare_csv.py`). |
| `gen_gk_reference.py` | Writes the reference table with SciPy, in a different algebraic form from the C++ code, fixed seed. |
| `tests/test_data_download.py`, `tests/test_gen_gk_reference.py`, `tests/test_gen_heston_reference.py`, `tests/test_gen_mc_reference.py`, `tests/test_gen_qe_reference.py`, `tests/test_gen_smile_reference.py`, `tests/test_gen_calibration_reference.py`, `tests/test_gen_barrier_reference.py`, `tests/test_gen_risk_reference.py`, `tests/test_gen_credit_reference.py`, `tests/test_docs_consistency.py` (documentation: contiguous finding and limitation numbers, matrix, cited files, version) | Offline tests of the parsers and of the generators (the Monte Carlo one needs no third-party package). |

## Other

| Path | Purpose |
|---|---|
| `cmake/` | Warning flags, sanitizer options, template for `find_package(fxval)`. |
| `examples/smile/` | `fxval-example-smile`: builds and checks the synthetic smile scenarios and writes the CSV behind the figures. |
| `configs/scenarios/` | `smile_scenarios.csv`: synthetic, illustrative smile quotes, shared by the example and the V-10 test. |
| `docs/results/` | Figures and data of the smile scenarios. |
| `examples/portfolio/` | `fxval-portfolio`: values, risk-measures and credit-adjusts an example FX book with the library's models. `portfolio_analysis.*` is the computation (a static library, `fxval_portfolio_core`), `report_writers.*` and `html_page.hpp` write the terminal, JSON and HTML output, `main.cpp` is the command line. Uses only the public API. |
| `examples/consumer/` | A tiny external project that links the installed library. |
| `scripts/mutation_check.py`, `scripts/mutants/*.json` | Plants each recorded change in a scratch copy of the repository and reports whether the tests notice (nightly in CI). |
| `scripts/run_fuzzers.sh` | Runs the libFuzzer targets for a fixed time on their corpus and regressions. |
| `.github/workflows/nightly.yml` | Long fuzzing and the mutation check, once a night and on demand. |
| `scripts/check.sh` | Runs format, builds, tests, sanitizers, clang-tidy and Python checks locally. |
| `.github/workflows/ci.yml` | The same checks in GitHub Actions, plus the compiler matrix, coverage and Docker. |
| `docs/` | Model documentation, test catalogue, architecture, ADRs, this guide. |
| `bench/bench_mc_scaling.cpp` | Monte Carlo benchmark: thread scaling, cost of Euler vs QE, SE^2 x time of the variance-reduced estimators. Build with `-DFXVAL_BUILD_BENCH=ON`; not part of `ctest` (timings are not tests). |
| `data/`, `notebooks/` | Placeholders, empty until used. |
