# Validation report: FX option pricing models, version 0.7

* **Models in scope:** Garman-Kohlhagen (benchmark), Heston semi-analytic pricer, Monte Carlo engine
  with Euler (full truncation) and Andersen QE schemes, antithetic and control variates.
* **Added in 0.6:** FX smile construction from delta quotes (one delta convention, quadratic smile,
  surface in total variance) with butterfly and calendar arbitrage checks.
* **Added in 0.7:** Heston calibration to delta quotes (bounded multi-start Levenberg-Marquardt on
  implied-volatility errors) with parameter-recovery, identification and noise tests (V-11).
* **Out of scope for this version:** Vanna-Volga and barrier models, VaR/ES, exposure and CVA (weeks 9 to 13;
  their sections will be added to this report).
* **Version tested:** library 0.7.0. The commit is in the metadata of every output file
  (section 8).
* **Status:** draft for release v0.5. Every number below comes from a test in this repository
  and can be reproduced with the commands in section 8. Numbers measured on one machine are
  marked as such.

## 1. Summary

| Component | Use | Evidence (tests) | Result |
|---|---|---|---|
| Garman-Kohlhagen price and Greeks | Benchmark, control variate, Greeks | V-01, V-02, GK-REF | No finding against the criteria. Greeks are refused at zero volatility or expiry (section 6, F-03). |
| Heston semi-analytic price | Reference price for Monte Carlo and, later, calibration | V-03, V-04, HESTON-REF | Matches an independent implementation to 1.6e-12 over 589 cases; stable over 576 extreme parameter sets. One defect found and fixed during development (F-01). |
| Monte Carlo engine | Pricing, later VaR and CVA scenarios | V-05, V-07, MC-PATH-REF, allocation tests | Statistically consistent with exact prices; results are bit-for-bit independent of the thread count. |
| Euler full truncation | Baseline scheme | V-05, V-06 | Large bias when the Feller condition fails (finding F-05). Kept as a baseline, not recommended. |
| QE scheme | Preferred Heston scheme | V-06, QE-PATH-REF | Unbiased to the resolution of the test at 32 steps on the four parameter sets studied; evidence limited to those sets (L-01). |
| Variance reduction | Efficiency | V-08 | Variance ratios 1.2 to 11.7; all estimators unbiased to the resolution of the test. |
| FX smile from delta quotes | Strike conversion, interpolation, arbitrage diagnostics (input to calibration and barriers) | V-09, V-10, SMILE-REF | Quotes, strikes and deltas agree to 2e-14 with an independent implementation and with the benchmark model's own Greeks. The quadratic smile has butterfly arbitrage near the money for an extreme risk reversal (F-08) and calendar arbitrage in the 5 to 11 delta wings for two of the four realistic scenarios (F-09); it always fails far enough out (F-10). Synthetic quotes only. |
| Heston calibration to smile quotes | Fit Heston to ATM, risk-reversal and butterfly quotes | V-11, CAL-REF, unit tests | Recovers the true parameters of 24 independent random sets from exact quotes (worst parameter error 3.7e-9). One expiry fits its three quotes but identifies nothing (24 of 24; standard errors infinite). Under 5 bp of noise the reported standard errors are up to a third too small (F-13). Tested on quotes made by Heston itself only (L-15). |
| Command-line program | Reproducible runs | unit, regression, end-to-end tests | Every output carries the hash of its input, the build, the seed and the time. |

**Overall assessment.** Within the parameter ranges and the European vanilla payoffs tested, the
pricing components behave as their specifications say, and the evidence for that does not rely on
the code under test alone (independent implementations, controls that must fail, hand mutation
checks). The model is **not** validated for trading or for any use outside the scope above. The
open points are in sections 6 and 7.

## 2. Purpose, intended use and materiality

The code is a portfolio project that mimics the work of a model validation function: it builds the
models and then tests them as an independent reviewer would. It prices European FX vanillas.
There is no exposure, no desk and no production use, so materiality is nil; the conclusions are
about the quality of the method and the code. A real model would need the same evidence plus
governance (ownership, approval, monitoring) that does not exist here.

## 3. Models

Specifications, derivations and numerical methods are in `docs/model_documentation.md`
(sections 3 to 5 and 4.6 to 4.11). In short:

* **Garman-Kohlhagen:** constant volatility, deterministic domestic and foreign rates, closed form.
* **Heston:** stochastic variance, `dv = kappa (theta - v) dt + xi sqrt(v) dW`, correlated with the
  spot. Price from one Fourier integral (Lewis 2001) with the numerically stable "little trap"
  characteristic function and adaptive Gauss-Kronrod quadrature.
* **Calibration:** Heston parameters fitted to the three pivot volatilities (25-delta put, ATM, 25-delta call) of each
  expiry by Levenberg-Marquardt on implied-volatility errors, logistic bounds, one data-driven and several
  counter-based random starts; Feller is not imposed.
* **Monte Carlo:** counter-based Philox4x32-10 random numbers (path number = stream number), fixed
  blocks merged in block order, Euler with full truncation or the QE scheme with martingale correction.

## 4. Validation approach

1. **Independent oracles.** Where a test needs "the right answer" it comes from code that shares nothing
   with the C++ implementation: SciPy for Garman-Kohlhagen (a different algebraic form), the textbook
   P1/P2 integrals with QUADPACK for Heston (a different integral, parametrisation and integrator), and a
   plain-Python Philox, Box-Muller, Euler and QE for the Monte Carlo paths. The QE oracle was written by the
   same author from the same paper as the C++ scheme, so it cannot detect a misreading of the paper; the exact
   conditional moments of the variance step (unit tests) and the comparison with the semi-analytic price
   (V-06) can.
2. **Controls that must fail.** A check that cannot fail proves nothing. The 1993 Heston formulation is run
   through V-04 and fails it (relative step 0.59 against 0.023 for the stable one); a naive thread reduction
   fails V-07 for 4 of 4 thread counts; Euler at 32 steps is rejected by V-06 (3.0 to 37 standard errors);
   a control variate with a 1% wrong mean is rejected at 40 standard errors in V-08.
3. **Criteria and seeds fixed before the results were seen,** and the places where an exploratory run
   had been seen are stated in the test file (V-06).
4. **Hand mutation checks.** Deliberate bugs were planted one at a time and the suite run: 5 changes
   to the Monte Carlo code in week 4, 15 to the QE scheme and variance reduction in week 5, 16 to the
   command-line layer in week 6, 25 to the smile code in week 7 and 28 to the calibration code in week 8 (tables in `docs/test_catalogue.md`).
   All are caught by something except one week-7 change that is equivalent in practice (a rounding-level
   threshold, section 7, L-13) and two week-8 changes that alter speed or an unused bracket and not any result.
   In week 8, four changes were first missed and led to new or tighter tests, and two are seen only by the
   V-11 noise study. Three
   Monte Carlo changes (a drift term in Euler, the missing martingale correction and a sign in `K1` in QE)
   are caught only by the independent path tables, which is a measure of how much weight those tables carry.
5. **Not automated:** the mutation checks are run by a script kept outside the repository and recorded by
   hand. They should become a CI job if the project grows.

## 5. Results

| ID | Test | Criterion | Result |
|---|---|---|---|
| V-01 | Put-call parity | relative error below 1e-12 | 8,000 markets; worst 4.0e-16 |
| V-02 | Greeks against finite differences | relative error below 1e-6 | 120 points; worst delta 1.8e-7, gamma 6.9e-8, vega 2.3e-8, volga 1.1e-7, vanna 2.3e-7, theta 6.6e-8, rho 2e-10 |
| GK-REF | C++ against SciPy | 1e-11 | 616 rows; worst 6.6e-14 |
| V-03 | Heston to Garman-Kohlhagen as `xi -> 0` | 1e-6 | exact at `xi = 0` (worst 4.2e-16, 60 cases); gap shrinks at the theoretical order; 2.8e-9 at `xi = 1e-7` |
| V-04 | Heston robustness | no branch-cut jumps, bounds respected | maturities to 30 years: worst relative step 0.023 (threshold 0.25; the 1993 formulation, as control, 0.59); 576 parameter sets (432 violate Feller): all prices finite, inside no-arbitrage bounds, decreasing and convex in strike |
| HESTON-REF | C++ against textbook integrals | 2e-11 absolute | 589 rows; worst 1.6e-12 |
| V-05 | Monte Carlo against exact prices | within 3 standard errors | engine with exact GBM: 16 comparisons, worst abs z 2.20; Heston Euler (Feller holds, 64 steps): worst abs z 1.69 |
| V-06 | Euler and QE when Feller fails | QE closer than Euler up to 16 steps; QE within 4 SE at 32 steps | 36 of 36 comparisons; 12 of 12 within 4 SE (worst 1.16); Euler 3.0 to 37 SE away at 32 steps |
| V-07 | Reproducibility | identical for any thread count | bitwise equal for 1, 2, 3, 4, 7, 8, 16 threads and the hardware count; 25 repeats |
| V-08 | Variance reduction | ratios reported; estimators unbiased | cost-adjusted ratios: antithetic 4.5 / 2.0 / 1.2, control 11.7 / 4.2 / 2.1, both 8.9 / 2.5 / 1.5 (strikes 0.95 / 1.10 / 1.25); all within 3 SE (worst 1.5) |
| MC-PATH-REF, QE-PATH-REF | C++ paths against plain Python | 1e-9 | 120 and 160 rows; worst 0 and 3.0e-15 |
| V-09 | Delta quote to strike round trip | quotes recovered to 1e-8 | 20,160 cases (both delta conventions, 25 and 10 delta, expiries 1 week to 5 years); worst delta error 2.0e-14, worst quote error 9.7e-17; a wrong-convention control misses by at least 1.2e-3 in 7,920 of 7,920 cases |
| V-10 | Smile arbitrage checks | violations reported per scenario; checker agrees with option-price convexity | 3,165 grid points, 67 with negative density, all agree; flat scenario clean; designed inversion and concave smile flagged as predicted; per-scenario counts in model documentation 6.7 |
| SMILE-REF | C++ smile against Python | strikes 1e-10 relative | 256 rows; worst strike 1.1e-15, volatility 7.8e-15, density factor 2.2e-13 |
| CAL-REF | Heston implied volatility and delta quotes against independent Python | 1e-8 | 160 implied volatilities, worst 2.1e-13; 120 quote sets, worst 7.5e-15 |
| V-11 (A) | Calibration recovers known parameters from exact quotes | volatility error 1e-6; parameters 1e-4 relative (rho absolute) | 24 sets (16 satisfy Feller, 8 violate it), five expiries each: worst volatility error 8.3e-12, worst parameter error 3.7e-9, all converged |
| V-11 (B) | One-expiry identification control | must fit the quotes, must not recover the parameters in at least 80% of sets, standard errors infinite | 24 of 24 fit (worst 4.3e-11), 24 of 24 not identified, 24 of 24 infinite |
| V-11 (C) | Noise study, 5e-4 on each pivot volatility | mean rms^2 within 25% of theory, spread within 0.6 to 1.6 of reported error, bias below 4 standard errors, all copies converge | 3 sets times 40 copies: rms^2 ratio 0.86, 0.94, 0.85; spread ratio 0.85 to 1.50; largest bias 3.45 (rho, set 16); 120 of 120 converged |
| ALLOC | No allocation in the path loop | 0 allocations per path; engine allocations independent of path count | passes for GBM, Euler, QE, antithetic and control variate; a model that allocates is detected (control) |

Test counts at version 0.7: 385 in `ctest` (unit, validation, command-line-layer, allocation and two end-to-end tests of
real executables; the week-8 additions are 44 unit tests, 2 CAL-REF tests and 3 V-11 tests); 75 Python tests. Notes on the numbers:

* V-06: QE's differences from the exact price are at the level of the sampling noise (standard errors of
  about 2e-4 to 7e-4), so the table shows no QE bias and cannot rule one out below that level.
* V-08: a linear control on the terminal spot beats antithetic variates here, and combining them is worse
  than the control alone because both exploit the same monotone dependence of the payoff on the noise.
* Throughput: 1.96 times faster on 2 cores, bitwise identical results (one machine, 2 hardware threads);
  scaling beyond 2 cores was not measured (`docs/model_documentation.md`, 5.10).

## 6. Findings

Severity: **medium** = could produce a wrong price in a plausible use; **low** = wrong or misleading
only in a corner case or for a diagnostic; **info** = a property to know. All items below were found by this
project's own tests, during development of the code they concern (the same person built and tested it).

| ID | Sev. | Finding | Found by | Status |
|---|---|---|---|---|
| F-01 | medium | The Heston characteristic function returned NaN at `phi(-i)` when `rho xi > kappa` (a removable singularity evaluated as 0/0). A price could become NaN for such parameters. | V-04 | Fixed: the value is returned as 1; `a + d` is computed as `xi^2 h / (d - a)` when its real part is negative. A reference-table block of 200 stress rows covers it. |
| F-02 | low | The quadrature stop rule could never be met at a tolerance of 1e-14, so the integrator reported non-convergence on integrals it had computed correctly. | quadrature unit tests | Fixed: a rounding-level stop rule (10 eps times the integral of |f|); truncation error and rounding level are reported separately. |
| F-03 | medium | In the Garman-Kohlhagen implementation reviewed in week 2, theta had the wrong sign for an in-the-money put at zero volatility. | a finite-difference probe | Fixed by refusing Greeks (`invalid_argument`) at zero volatility or expiry, where they are not defined; prices remain defined there. V-02 covers positive volatility only. |
| F-04 | low | A volga check by second difference of the price failed at short expiries because rounding noise grows like eps/h^2. | V-02 development | Changed the test method: volga is differenced from the analytic vega. No code defect. |
| F-05 | medium | Euler with full truncation is badly biased when the Feller condition fails (bias 3.4e-2 to 1.7e-3 for 8 to 128 steps in the first case studied, still 7.8 SE at 128 steps). | V-05, V-06 | Mitigated: QE added; Euler kept as a documented baseline. |
| F-06 | low | A small error in one drift term of the Euler scheme (raw `v` instead of `v+` in the variance drift) is invisible to statistical comparison at moderate path counts. | hand mutation check | Closed: independent path table (MC-PATH-REF) catches it; recorded as the reason the table exists. |
| F-07 | low | The `1 - U` tail computation in QE's exponential branch matters only for `z` beyond about 8; the first mutation run found nothing that detected a loss of precision there. | hand mutation check (week 5) | Closed: unit test calls the variance step at `z` = 6 to 8.4 and checks the tail growth against the Gaussian tail. |
| F-08 | low | With an extreme risk reversal (-8 vol points on a 10% ATM, so the 25-delta put is 14.3% and the 25-delta call 6.3%) the quadratic smile has negative implied density over roughly -1.1 to -0.1 standard deviations (figure `docs/results/smile_extreme_skew.png`; innermost violation 0.12 to 0.27 standard deviations from the forward, about 40 delta) and a non-positive volatility in the wings. Quotes this skewed are outside any realistic G10 range, but the construction accepts them. | V-10 report | Open: reported by `check_butterfly`; the smile is not repaired. A caller must check the report. |
| F-09 | medium | The interpolated surface has calendar arbitrage in the wings for two of four realistic scenarios: stressed 3M to 6M (34 of 801 grid points) and 1Y to 2Y (61), emerging 3M to 6M (41) and 6M to 1Y (86). The violations start 3.5 to 3.8 standard deviations from the forward, at 5 to 11 absolute delta, and every slice is individually clean. Barrier and touch prices that depend on that range (week 9) could be inconsistent across maturities. A likely cause, not tested separately, is that the risk reversal and butterfly term structure moves faster than ATM total variance grows. | V-10 report | Open: reported by `check_calendar`. Next step: a surface constructed with the condition built in (candidate for v1.1) or a restriction to the range where the check passes. |
| F-10 | info | A quadratic volatility smile violates the butterfly condition far enough from the forward whatever the quotes (total variance grows like `k^4`). For one calm 1-year smile (ATM 8%, RR -0.75, BF 0.28 vol points) the density factor is still positive at 15 standard deviations and negative at 20. | theory, probe | Accepted: a property of the construction, outside any traded strike range. |
| F-11 | low | `heston_delta_quotes` searched each wing volatility starting from five times the model volatility level. For a 2-year, strongly skewed set (rho = -0.6, spot delta) the 25-delta call strike at that volatility is eight forwards away, its price underflows and the search failed with an error. A second case, found while fixing the first (kappa = 0.3, xi = 1, T = 2, low variance), failed because the ATM volatility is below 0.3 times the level, the fixed lower end. The function returned an error, never a wrong value. | CAL-REF (the first); a probe (the second) | Fixed: both ends of the bracket are scanned outwards from the level and stop at the first sign change. Regression tests for both. Four further extreme sets (for example v0 = 0.01, kappa = 0.2, theta = 0.09, xi = 2, rho = -0.9, T = 1) failed with a bracket error before the fix and all succeed after it (probe, not a committed test). |
| F-12 | info | One expiry gives three quotes for five Heston parameters. The calibration fits them (error 4.3e-11) and returns parameters that are wrong by more than 1% in 24 of 24 sets, while a naive reading of the fit quality would call it perfect. | V-11 (B) | Accepted and handled: standard errors are reported as infinite when the data cannot determine them; use at least two or three expiries. |
| F-13 | low | The linearised standard errors are too small in the noise study: the observed spread of the estimates is 1.06 to 1.50 times the reported error on two of three sets, and on set 16 the mean of 40 estimates of rho, xi and theta lies 3.45, 2.61 and 2.22 standard errors of the mean from the truth, which points to a small bias at 5 bp of noise. All criteria were met; the margin on the bias criterion (4) is small. | V-11 (C) | Open: report standard errors as indicative (L-14). A larger noise study (more copies and sets) would show whether the bias is real. |

## 7. Limitations and open points

* **L-01 (QE evidence):** QE was measured at four Feller-violating parameter sets, three strikes and
  five step counts. It is not known to be unbiased in general. Deep out-of-the-money prices and
  expiries beyond 3 years were not studied.
* **L-02 (QE martingale fallback):** with very long steps and strongly positive `rho` the martingale
  correction does not exist and the scheme falls back to the uncorrected form (finite, not exactly a
  martingale). Eight of the 160 path-table rows take this branch; no pricing test is in that regime.
* **L-03 (independence):** the author built and validated the models. Independence is technical
  (independent implementations, controls, mutation checks), not organisational. A real validation
  would be done by a second person who could also disagree with the specification itself.
* **L-04 (scope of the models):** deterministic rates, constant day-count convention (ACT/365 fixed),
  European payoffs on the terminal spot only, no Heston Greeks; the smile and the calibration use synthetic quotes (L-09 to L-12, L-15).
* **L-05 (not yet run):** the GitHub Actions workflows, the Docker build, the vcpkg build and the
  g++-14 and clang-17 builds have not been run. The tests were run with g++ 13 and clang 18 in release,
  debug, AddressSanitizer plus UBSan and ThreadSanitizer builds.
* **L-06 (timings):** throughput numbers come from one 2-core machine and are not asserted by any test.
* **L-07 (fuzzing):** the configuration parser is tested with thousands of randomly damaged documents in
  every build, but libFuzzer targets are not yet in place (planned for weeks 11 to 14).
* **L-08 (shared random numbers):** all options in a run use the same seed, so they share random
  numbers. This is deliberate and documented; it makes price differences smoother and standard errors
  of differences smaller than independent runs would give.
* **L-09 (delta conventions):** only premium-excluded delta with a delta-neutral ATM is implemented, and the
  simple (not the market-strangle) butterfly. A currency pair quoted with premium-adjusted delta or an
  ATM-forward definition is not reproduced.
* **L-10 (smile form):** the quadratic is exact at three points and arbitrage-free only where the
  checks say so (F-08 to F-10). Ten-delta quotes cannot be added to 25-delta quotes in this construction.
* **L-11 (synthetic quotes):** all smile inputs are made up. Their levels are plausible for G10 pairs,
  but the findings F-08 and F-09 describe those inputs, not any market.
* **L-12 (check grid):** the checks look at 801 points between plus and minus 4 standard deviations. A
  violation between two grid points, or beyond the grid, is not seen.
* **L-13 (one unobservable mutation):** the sign of the rounding-level slack in the butterfly check
  can be changed without any test noticing, because no input produces a density factor within 1e-12 of 0.
* **L-14 (standard errors):** the calibration's standard errors are linearised. In the noise study the observed
  spread was up to 1.5 times the reported error (F-13); treat them as indicative.
* **L-15 (inverse crime):** the calibration is tested on quotes generated by Heston itself (by independent code).
  This shows that the optimiser and the maps are correct and that parameters are identified from five expiries; it
  does not show that Heston can reproduce a real or arbitrary smile, which is a different question.
* **L-16 (calibration scope):** three quotes per expiry at one delta, equal weights, no regularisation, no Feller
  constraint. Several starts lower the chance of a local minimum but do not prove a global one. Extreme parameter
  sets can still make `heston_delta_quotes` fail with a bracket error (never a wrong value). The noise study covers
  3 sets and 40 copies each.

## 8. Reproducing these results

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release   # 385 tests
python -m pytest -q                                                                    # needs numpy, scipy, jsonschema
build/release/examples/smile/fxval-example-smile configs/scenarios/smile_scenarios.csv docs/results/smile_data
python python/plot_smiles.py docs/results/smile_data docs/results                      # needs matplotlib
build/release/apps/fxval-price --config configs/examples/heston_mc_qe.json --output run.json
sha256sum configs/examples/heston_mc_qe.json   # equals metadata.config_sha256 in run.json
```

Every output file records the library version, git commit (`-dirty` if tracked files differed), compiler,
build type, SHA-256 of the exact input bytes, the seed and the UTC time. With `SOURCE_DATE_EPOCH` set, the
same input and build give a byte-identical file. The path tables are regenerated by the scripts in
`python/` and CI compares them with the committed files.

## 9. Conclusion and conditions of use

The components in scope are fit for their stated purpose in this project: as a benchmark, a reference
and a tested Monte Carlo engine for European FX vanillas. Use outside the tested parameter ranges, for
other payoffs, or for any decision with money attached requires the open points in section 7 to be
closed and an independent review. Findings F-01 to F-07 and F-11 are closed or mitigated. F-08, F-09 and F-13 are open (F-12 is a property of the problem): the smile and surface
construction is fit as a source of three pivot volatilities per tenor, and not as an arbitrage-free
surface for pricing options across the wings. F-10 is a property of the construction. The calibration is fit as a tested tool for
fitting Heston to synthetic Heston-generated smiles with five expiries; it has not been shown on a market smile (L-15).
