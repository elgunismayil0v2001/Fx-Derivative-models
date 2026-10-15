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
| V-05 | Heston Monte Carlo (each scheme) vs semi-analytic | Within 3 standard errors; bias vs step size reported | 4 | passing (Euler full truncation; QE added in week 5) | `tests/validation/test_v05_heston_mc_vs_analytic.cpp`. Engine check with exact GBM vs Garman-Kohlhagen: 16 comparisons, worst abs z 2.20. Heston Euler, Feller satisfied, 64 steps, 3 strikes: worst abs z 1.69. Bias vs steps in a Feller-violating case: 3.4e-2, 1.9e-2, 9.2e-3, 4.2e-3, 1.7e-3 for 8 to 128 steps; still 7.8 SE at 128 steps, which is also the power check (the test can reject a biased scheme). Assertions and seed were fixed before the results were seen. Fewer paths are used in Debug and sanitizer builds. |
| V-06 | Scheme behaviour when the Feller condition fails | Bias documented for Euler full truncation and QE | 5 | planned | |
| V-07 | Monte Carlo reproducibility | Identical result for 1 and N threads with the same seed | 4 | passing | `tests/validation/test_v07_mc_reproducibility.cpp`. Bitwise equal (mean, standard error, variance) for 1, 2, 3, 4, 7, 8, 16 threads and hardware concurrency, GBM and Heston; 25 repeats with 8 threads. Control: a naive equal-share-per-thread sum differs from its 1-thread value for 4 of 4 thread counts, so the check can fail. Block size changes only rounding (agreement 1e-12), different seed changes the result. |
| V-08 | Variance reduction | Variance ratio reported for control variate and antithetics | 5 | planned | |
| V-09 | Delta-quote to strike conversion round trip | Recovered quote error below 1e-8 | 7 | planned | |
| V-10 | Smile arbitrage checks | Butterfly and calendar violations reported per scenario | 7 | planned | |
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
* **Mutation check, done by hand in week 3** (not automated): flipping the sign of `rho` in the
  characteristic function is caught by HESTON-REF and the smile unit test but **not** by V-03 or
  V-04 (those do not depend on the sign of `rho`); corrupting one Gauss-Kronrod weight is caught by
  11 tests; flipping a sign in `D` is caught by HESTON-REF, V-03, V-04 and a unit test. This is why
  the independent table is part of the suite.

## Tests that are not part of the V-series

Unit tests (`tests/unit/`) cover the library's own behaviour: the error type, typed units, day
count, known GK values, degenerate inputs, Heston parameter validation, characteristic-function
identities, the smile sign, the quadrature routine, the Philox known answers and normal-stream
statistics, running statistics, engine configuration errors and the martingale property of the
simulated spot, and error paths. They protect the code from regressions
and are not validation evidence about the models, so they are not listed here. See
`docs/adr/0003-testing-strategy.md`.
