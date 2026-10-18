# Validation test catalogue

Each test has an ID, a pass criterion and a status. A test is `planned` until it exists in
`tests/validation/` as a named test and passes in CI, then it becomes `passing` and gets a link
to its result in `validation_report.md`.

Tolerances are starting points. Changing one needs a reason written in the **Notes** column.

`tests/validation/test_catalogue_consistency.cpp` fails the build if an ID is missing,
duplicated, or outside V-01 to V-21.

| ID | Test | Pass criterion | Planned week | Status | Notes |
|---|---|---|---|---|---|
| V-01 | Put-call parity (Garman-Kohlhagen) | Error below 1e-12 | 2 | passing | `tests/validation/test_v01_put_call_parity.cpp`. 8000 markets (5 spots x 5 strikes at 0.6-1.8 of spot x 4 domestic x 4 foreign rates from -1% to 8% x vol 1%-100% x expiry 1 day-30 years). Worst relative error 4.0e-16. |
| V-02 | GK Greeks vs finite differences | Relative error below 1e-6 (central difference) | 2 | passing | `tests/validation/test_v02_greeks_fd.cpp`. 120 points, 5-point fourth-order stencils. Worst: delta 1.8e-7, gamma 6.9e-8, vega 2.3e-8, volga 1.1e-7, vanna 2.3e-7, theta 6.6e-8, rho 2e-10. Volga is differenced from the analytic vega because a second difference of the price is limited by rounding noise (eps/h^2). |
| V-03 | Heston semi-analytic vs GK as vol-of-vol goes to 0 | Prices agree within 1e-6 | 3 | passing | `tests/validation/test_v03_heston_vs_gk.cpp`. Exact at xi = 0 (worst 4.2e-16 on 60 cases). For xi > 0 the gap shrinks at the theoretical order: tenfold per tenfold in xi for rho != 0, hundredfold for rho = 0 (checked as ratios). Worst gap at xi = 1e-7: 2.8e-9. |
| V-04 | Heston semi-analytic robustness over long maturities and extreme parameters | No branch-cut or discontinuity errors over a parameter grid | 3 | passing | `tests/validation/test_v04_heston_robustness.cpp`. (A) characteristic function walked along maturity to 30 years on 72 parameter/point combinations: worst relative step 0.023 against a jump threshold of 0.25; the original 1993 formulation, run as a control, shows 0.59, so the check can fail. (B) 576 parameter sets (432 violate Feller; vol of vol up to 5, rho +-0.95, kappa 0.05 to 10, expiry 1 week to 30 years) x 5 strikes: every price finite, inside the no-arbitrage bounds, decreasing and convex in strike, and the characteristic function respects its martingale bounds. |
| V-05 | Heston Monte Carlo (each scheme) vs semi-analytic | Within 3 standard errors; bias vs step size reported | 4 | passing (Euler full truncation; QE in V-06) | `tests/validation/test_v05_heston_mc_vs_analytic.cpp`. Engine check with exact GBM vs Garman-Kohlhagen: 16 comparisons, worst abs z 2.20. Heston Euler, Feller satisfied, 64 steps, 3 strikes: worst abs z 1.69. Bias vs steps in a Feller-violating case: 3.4e-2, 1.9e-2, 9.2e-3, 4.2e-3, 1.7e-3 for 8 to 128 steps; still 7.8 SE at 128 steps, which is also the power check (the test can reject a biased scheme). Assertions and seed were fixed before the results were seen. Fewer paths are used in Debug and sanitizer builds. |
| V-06 | Scheme behaviour when the Feller condition fails | Bias documented for Euler full truncation and QE | 5 | passing | `tests/validation/test_v06_feller_study.cpp`. Four Feller-violating sets (ratio 0.03 to 0.1), 3 strikes, 4 to 64 steps, 100,000 paths. QE closer than Euler in 36 of 36 comparisons up to 16 steps (typically 50 to 150 times closer at 4 steps); QE within 4 SE of the semi-analytic price at 32 steps for all 12 set-strike pairs (worst 1.16 SE); Euler 3.0 to 37 SE away there (power check). QE martingale at 16 steps; insensitive to the switching level; Feller-ratio sweep recorded. The full table is in model documentation 5.8. Assertions and seed fixed before the results; one exploratory run on set A had been seen (stated in the file). |
| V-07 | Monte Carlo reproducibility | Identical result for 1 and N threads with the same seed | 4 | passing | `tests/validation/test_v07_mc_reproducibility.cpp`. Bitwise equal (mean, standard error, variance) for 1, 2, 3, 4, 7, 8, 16 threads and hardware concurrency, GBM and Heston; 25 repeats with 8 threads. Control: a naive equal-share-per-thread sum differs from its 1-thread value for 4 of 4 thread counts, so the check can fail. Block size changes only rounding (agreement 1e-12), different seed changes the result. |
| V-08 | Variance reduction | Variance ratio reported for control variate and antithetics | 5 | passing | `tests/validation/test_v08_variance_reduction.cpp`. Heston QE, 200,000 paths, strikes 0.95 / 1.10 / 1.25. Cost-adjusted variance ratios: antithetic 4.5 / 2.0 / 1.2, control (`S_T`) 11.7 / 4.2 / 2.1, both 8.9 / 2.5 / 1.5. All estimators within 3 SE of the exact price (worst 1.5); a control with a 1% wrong mean rejected at 40 SE (power check); reported SE matches the spread over 80 seeds within 30%. Under GBM a Garman-Kohlhagen price works as a control (ratio 79). |
| V-09 | Delta-quote to strike conversion round trip | Recovered quote error below 1e-8 | 7 | passing | `tests/validation/test_v09_delta_roundtrip.cpp`. 20,160 cases (3 spots x 4 rate pairs x 7 expiries from 1 week to 5 years x 4 ATM levels x 5 risk reversals x 3 butterflies x 25 and 10 delta x spot and forward delta). The quoted delta is recovered from the library's own Garman-Kohlhagen Greeks at the computed strikes: worst 2.0e-14; ATM straddle delta worst 3.3e-14; the smile returns the three pivot volatilities (worst 5.6e-17) and the quotes come back (worst 9.7e-17). Control: judging the same strikes in the other delta convention misses by at least 1.2e-3 in all 7,920 cases with `r_f T` above 0.01, so the check can see a convention mix-up. Criterion fixed before the results; one convention implemented (docs section 6.2). |
| V-10 | Smile arbitrage checks | Butterfly and calendar violations reported per scenario | 7 | passing | `tests/validation/test_v10_smile_arbitrage.cpp`. Pre-registered: (1) the density factor `g` agrees with the second difference of Garman-Kohlhagen option prices (3,165 grid points over every slice of 8 scenarios, 67 with negative density, so the sign check has power), (2) the flat scenario is clean, (3) calendar inversion (1 week 20% to 2 weeks 9%) is flagged at every grid point and the next pair is clean, and the concave scenario is flagged. Reported per scenario (section 6.7 of the model documentation): calendar violations in the wings of the stressed and emerging scenarios (3.5 or more standard deviations, 5 to 11 delta), butterfly violations in the extreme-skew scenario. A frozen "observed" test keeps the counts. The first run used a stencil step that was too coarse for the concave smile; changed once, disclosed in the file. |
| V-11 | Heston calibration parameter recovery | Parameters and implied vols recovered within stated tolerance across several seeds | 8 | planned | |
| V-12 | Barrier: GK closed form vs Monte Carlo with Brownian bridge | Within 3 standard errors | 9 | planned | |
| V-13 | Barrier: in-out parity | Knock-in plus knock-out equals vanilla within tolerance | 9 | planned | |
| V-14 | Vanna-Volga reproduces the three pivot market prices | Exact to numerical precision | 9 | planned | |
| V-15 | Barrier model comparison (GK vs Vanna-Volga vs Heston) | Spread table produced; model risk quantified | 9 | planned | |
| V-16 | GARCH estimation vs Python `arch` | Parameters and log-likelihood within tolerance | 10 | planned | |
| V-17 | VaR backtests (each method) | Exceptions, Kupiec and Christoffersen p-values, traffic-light zone reported | 10 | planned | |
| V-18 | Delta-normal vs full revaluation for options | Gap in VaR quantified | 10 | planned | |
| V-19 | Single-forward expected exposure vs Black-76 | Within 3 standard errors | 11 | planned | |
| V-20 | CVA: Monte Carlo exposure vs nested simulation on a small case | Agreement within stated tolerance | 11 | planned | |
| V-21 | Look-ahead and leakage check in backtests | Proof that VaR at date t uses data up to t-1 only | 10 | planned | |

## Other validation evidence (not a V-number)

* **GK-REF** (`tests/validation/test_gk_reference.cpp`): C++ price and Greeks against an
  independent Python (SciPy) implementation, 616 rows in `tests/validation/data/gk_reference.csv`
  (8 boundary cases plus 300 random markets, call and put, seed 20261010). Tolerance 1e-11; worst
  difference 6.6e-14. CI regenerates the table and fails if it differs from the committed file.

* **HESTON-REF** (`tests/validation/test_heston_reference.cpp`): C++ call and put against an
  independent Python implementation (Heston 1993 probabilities P1 and P2, SciPy QUADPACK), 589
  rows in `tests/validation/data/heston_reference.csv` (389 moderate plus 200 stress rows with
  `rho xi > kappa`; rows where SciPy cannot certify its own integral are dropped). Tolerance
  `2e-11 * max(1, spot)` absolute; worst difference 1.6e-12. The table is compared numerically,
  not byte by byte, because quadrature results can differ in the last digits between SciPy versions.
* **MC-PATH-REF** (`tests/validation/test_mc_path_reference.cpp`): C++ normals and Heston Euler
  paths against an independent pure-Python implementation of Philox4x32-10, Box-Muller and the
  scheme, 120 rows in `tests/validation/data/mc_path_reference.csv` (64-bit seeds and streams,
  `xi = 0`, `rho = +-1`, one step, Feller violations). Tolerance 1e-13 (normals) and 1e-9
  (paths); worst difference on the development machine 0. Compared numerically because libm
  `log`/`cos`/`sin`/`exp` can differ in the last digits between machines. Python's Philox
  reproduces the three published known-answer vectors, which are also unit tests in C++.
* **QE-PATH-REF** (`tests/validation/test_mc_qe_path_reference.cpp`): C++ QE terminal states
  against the independent Python implementation `python/gen_qe_reference.py`, 160 rows in
  `tests/validation/data/mc_qe_path_reference.csv` (44 antithetic, 8 that take the martingale
  fallback, both variance branches, `psi_c` = 1 and 2, 64-bit seeds and streams, one-step paths).
  Tolerance 1e-9; worst difference 3.0e-15 (spot, relative) and 8.7e-16 (variance, absolute). Same
  author and same paper for both implementations: a misreading of the paper would be in both, so
  V-06 and the exact-moment unit tests are the checks that do not share that risk.
* **SMILE-REF** (`tests/validation/test_smile_reference.cpp`): strikes, pivot volatilities, smile
  values and the butterfly density factor against `python/gen_smile_reference.py`, 256 rows in
  `tests/validation/data/smile_reference.csv` (6 boundary cases and 250 random ones: both delta
  conventions, 25 and 10 delta, expiries from 1 week to 5 years, rates from -1% to 8%, spot 0.5 to
  150). The Python strikes come from root finding on the spot-form Garman-Kohlhagen delta, not from
  the closed-form inverse the C++ uses; the smile is fitted with a Vandermonde solve (the C++ uses
  divided differences). Worst differences: strikes 1.1e-15 relative, volatilities 7.8e-15 absolute,
  density factor 2.2e-13. The density-factor formula (Gatheral 2004) is shared by the two
  implementations; V-10 checks it against option prices, which do not use it.
* **Mutation check, done by hand in week 4** (not automated). Each change below was made to the
  Monte Carlo code, the suite was run, and the change was reverted:

  | Change | Caught by |
  |---|---|
  | sign of `rho` flipped in the spot shock | V-05 (two tests), MC-PATH-REF |
  | raw `v` instead of `v+` in the spot drift | martingale unit test, MC-PATH-REF; **not V-05** |
  | raw `v` instead of `v+` in the variance drift | MC-PATH-REF only; **not V-05, not any unit test** |
  | one Philox multiplier changed in the last digit | known-answer test (compile-time `static_assert`), MC-PATH-REF |
  | paths `2k` and `2k+1` share a stream | stream-number unit tests, V-05 |

  The third row is why the independent path table exists: a statistical comparison with
  moderate path counts cannot see a small error in one drift term.
* **Mutation check, done by hand in week 5** (not automated). Fifteen changes were made one at a time
  to the QE scheme, the antithetic stream or the variance-reduction code; the Release build was rebuilt
  and the new unit tests plus QE-PATH-REF, V-05, V-06, V-08 and MC-PATH-REF were run each time (script
  kept outside the repository). Each change was reverted afterwards.

  | Change | Caught by |
  |---|---|
  | sign of `rho/xi` flipped in `K2` | QE-PATH-REF, V-06 (4 tests), V-08 |
  | sign of `rho/xi` flipped in `K1` | **QE-PATH-REF only** |
  | martingale correction never applied | **QE-PATH-REF only** (the uncorrected scheme is already a martingale to within the resolution of the V-06 and unit martingale checks) |
  | `A = K2` instead of `K2 + K4/2` in the correction | martingale unit test, QE-PATH-REF, V-06 (4 tests), V-08 |
  | `K3` 10% too large | QE-PATH-REF, V-06 (2 tests), V-08 |
  | spot shock built from `Z_v` instead of `Z_perp` | martingale unit test, QE-PATH-REF, V-06 (4 tests), V-08 (2 tests) |
  | `p = psi/(psi+1)` in the exponential branch | point-mass and exact-moment unit tests, QE-PATH-REF, V-06 |
  | constant term of `s2` divided by `kappa` instead of `2 kappa` | point-mass and exact-moment unit tests, QE-PATH-REF, V-06 (2 tests), V-08 |
  | `theta` term of `m` 1% too large | point-mass unit test, QE-PATH-REF |
  | `1 - U` computed as `1.0 - u` instead of from `erfc` | **caught only after a test was added for it**: the first run of this check found nothing (the loss of precision affects only draws with `z` beyond about 8, which a standard error cannot see); the unit test `ExponentialBranchRightTailStaysAccurateForLargeNormalDraws` now calls the variance step directly at `z` = 6 to 8.4 and checks growth against the Gaussian tail |
  | antithetic flag ignored by `NormalStream` | antithetic stream unit test, odd-function unit test, QE-PATH-REF, V-08 |
  | antithetic pair simulates the same path twice | odd-function unit test, V-08 |
  | pilot run uses the same streams as the main run | pilot-stream unit test only |
  | control mean added instead of subtracted | perfect-control unit test, V-08 (3 tests) |
  | control coefficient twice too large | pilot-coefficient unit tests (2), V-08 (GBM control) |

  Two mutations are seen by the independent path table only, which is the reason the table exists:
  a statistical comparison with moderate path counts cannot see a sign error in one drift term, nor the
  missing martingale correction. The `1 - U` mutation was the one the suite missed at first; the extra
  unit test is the fix, and it was itself checked by re-running that mutation (now caught).
* **Mutation check, done by hand in week 6** (not automated). Sixteen changes were made one at a time to
  the configuration parser, runner, report writer, CLI, metadata and SHA-256 code; the Release build was
  rebuilt and the command-line-layer tests, the SHA-256 tests and the end-to-end test were run each time
  (script kept outside the repository). One change did not compile the first time (an unused parameter) and
  was rewritten; each was reverted afterwards. Every change is caught. The list of tests is abbreviated.

  | Change | Caught by |
  |---|---|
  | duplicate option ids allowed | the parameterised rejection tests (several cases)|
  | unknown keys ignored | the parameterised rejection tests (several cases) |
  | repeated JSON keys allowed | PriceConfigSyntaxTest.RepeatedKeysAreRejectedNotSilentlyResolved |
  | domestic and foreign rate swapped | PriceConfigAcceptTest.GarmanKohlhagenValuesAreConverted; RegressionTest.GarmanKohlhagenVanillas; RegressionTest.HestonMonteCarloQe; RegressionTest.HestonSemiAnalyticSmile; the parameterised rejection tests (several cases)|
  | seed optional (defaults to 0) | the parameterised rejection tests (several cases)|
  | present value ignores notional | PriceRunnerTest.PresentValueIsPriceTimesNotional; RegressionTest.GarmanKohlhagenVanillas |
  | failed options count as success | CliTest.FailedOptionStillProducesTheReportAndExitsThree |
  | hash of the wrong bytes | RunMetadataTest.MetadataRecordsTheInputHashVersionAndTime; cli_end_to_end |
  | month off by one in timestamps | CliTest.EpochOverrideAppearsInBothFormats; RunMetadataTest.TimestampMatchesPythonDatetime |
  | size limit off by one | CliTest.ConfigurationLargerThanTheLimitIsRejected |
  | CSV does not quote commas | CsvFieldTest.QuotesOnlyWhenNeeded |
  | SHA-256 sigma constant wrong | Sha256Test.OneMillionA; Sha256Test.PaddingBoundaries; Sha256Test.PublishedVectors; cli_end_to_end |
  | report written non-atomically (rename skipped, tmp kept) | CliTest.ReportCanBeWrittenToAFileAtomically |
  | QE allowed with xi = 0 | the parameterised rejection tests (several cases)|
  | CLI log written to stdout | CliTest.ConfigurationCanComeFromStandardInput; CliTest.ConfigurationLargerThanTheLimitIsRejected; CliTest.FailedOptionStillProducesTheReportAndExitsThree; CliTest.InlineEqualsFormIsAccepted; CliTest.InvalidConfigurationExitsTwoNamesTheFieldAndPricesNothing; CliTest.MissingConfigurationFileExitsFour, and others |
  | antithetic flag dropped from config | PriceConfigAcceptTest.HestonSemiAnalyticAndMonteCarloValuesAreConverted; RegressionTest.HestonMonteCarloQe; ReportJsonTest.MonteCarloReportEchoesSeedAndSettings; the parameterised rejection tests (several cases)|
* **Mutation check, done by hand in week 7** (not automated). Twenty-five changes were made one at a
  time to the normal quantile, delta conversion, quote, smile, surface and arbitrage-check code; the
  Release build was rebuilt and the smile unit tests, V-09, SMILE-REF and V-10 were run each time (script
  kept outside the repository). Three changes did not compile the first time (unused variable under
  `-Werror`) and were rewritten; each change was reverted afterwards. **Twenty-four are caught; one
  survives.**

  | Change | Caught by |
  |---|---|
  | spot delta: foreign discount factor omitted | 9 tests (strike conversion unit tests, SMILE-REF) |
  | put strike: sign of `d1` not flipped | 35 tests |
  | ATM strike: sign of `sigma^2 T / 2` | ATM straddle unit test, SMILE-REF and others (7) |
  | put delta sign in `delta_at_strike` | 3 tests |
  | risk reversal sign swapped | 9 tests |
  | butterfly ignored | 15 tests |
  | quote delta range loosened to below 1 | one unit test (`QuoteDeltaMustBeAnOutOfTheMoneyDelta`) |
  | curvature divided by the wrong span | pivot-recovery unit test, SMILE-REF and others (7) |
  | pivot ordering check removed | one unit test (`InconsistentRateAndDeltaGiveOrderedStrikesError`) |
  | first derivative of the smile: curvature term halved | derivative unit test, SMILE-REF and others (6) |
  | second derivative missing the factor 2 | derivative unit test, SMILE-REF and others (6) |
  | density factor: sign of `w''/2` | 7 tests |
  | density factor: `1/w + 1/4` written as `1/w - 1/4` | 6 tests |
  | `w''` without the `sigma sigma''` term | 8 tests |
  | density factor: sign of `k w'/(2w)` | SMILE-REF, V-10 price convexity, V-10 observed (4) |
  | calendar check direction reversed | 8 tests |
  | calendar grid taken from the later slice | **V-10 observed test only** |
  | grid does not reach `x_max` | **V-10 observed test only** |
  | surface interpolates volatility instead of total variance | one unit test |
  | surface interpolation weight inverted | 3 tests |
  | first expiry excluded from the quoted range | one unit test |
  | normal quantile: Halley step sign | 6 tests |
  | normal quantile: upper tail by symmetry dropped | 5 tests |
  | forward built with `r_d + r_f` | 7 tests |
  | **butterfly slack sign (`g < +1e-12` instead of `g < -1e-12`): survives** | none. Equivalent in practice: it differs only for a point with `|g| < 1e-12`, which no input here produces. A smile engineered to have `g = 0` on a grid point would be needed to test it. |

  Two changes are seen only by the frozen "observed" V-10 test, which says how much weight that test
  carries: it is a regression test of the scenario table, not a test of theory.
* **Mutation check, done by hand in week 3** (not automated): flipping the sign of `rho` in the
  characteristic function is caught by HESTON-REF and the smile unit test but **not** by V-03 or
  V-04 (those do not depend on the sign of `rho`); corrupting one Gauss-Kronrod weight is caught by
  11 tests; flipping a sign in `D` is caught by HESTON-REF, V-03, V-04 and a unit test. This is why
  the independent table is part of the suite.

## Tests that are not part of the V-series

Unit tests (`tests/unit/`) cover the library's own behaviour: the error type, typed units, day
count, known GK values, degenerate inputs, the normal quantile, delta conversion, quote and smile objects, surface interpolation and the arbitrage-check reports, Heston parameter validation, characteristic-function
identities, the smile sign, the quadrature routine, the Philox known answers and normal-stream
statistics, running statistics, engine configuration errors and the martingale property of the
simulated spot, and error paths. They protect the code from regressions
and are not validation evidence about the models, so they are not listed here. See
`docs/adr/0003-testing-strategy.md`.
