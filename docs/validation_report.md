# Validation report: FX option pricing models, version 0.9

* **Models in scope:** Garman-Kohlhagen (benchmark), Heston semi-analytic pricer, Monte Carlo engine
  with Euler (full truncation) and Andersen QE schemes, antithetic and control variates.
* **Added in 0.6:** FX smile construction from delta quotes (one delta convention, quadratic smile,
  surface in total variance) with butterfly and calendar arbitrage checks.
* **Added in 0.7:** Heston calibration to delta quotes (bounded multi-start Levenberg-Marquardt on
  implied-volatility errors) with parameter-recovery, identification and noise tests (V-11).
* **Added in 0.8:** barrier, one-touch and double no-touch options (closed forms and Brownian-bridge Monte
  Carlo), Vanna-Volga (three variants) and a comparison of Garman-Kohlhagen, Vanna-Volga and Heston with the
  spread reported as model risk (V-12 to V-15, BARRIER-REF).
* **Added in 0.9:** market risk: EWMA and GARCH(1,1)/GJR-GARCH maximum-likelihood estimation, historical, normal,
  Cornish-Fisher, delta-gamma, full-revaluation and filtered-historical VaR and ES for a spot-and-options portfolio,
  and backtests (Kupiec, Christoffersen, Basel traffic light) with a look-ahead check (V-16, V-17, V-18, V-21,
  RISK-REF).
* **Out of scope for this version:** exposure and CVA (weeks 11 to 13; their sections will be added to this
  report).
* **Version tested:** library 0.9.0. The commit is in the metadata of every output file
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
| Barrier, touch and double no-touch options | Exotic pricing, benchmark for the model comparison | BARRIER-REF, V-12, V-13 | Closed forms match independent numerical integration to 6.7e-16 on 700 cases; the Monte Carlo with the Brownian bridge agrees within 3 standard errors for 8 contracts; discrete monitoring is biased as expected (control). One defect found and fixed (F-14). Heston bridge is approximate (L-17). |
| Vanna-Volga and model comparison | Smile adjustment; model-risk spread | V-14, V-15 | Pivots reproduced to 1e-11. Spread between Garman-Kohlhagen, Vanna-Volga and Heston is a few vol points for vanillas and up to 35% of notional for touch products on the most skewed synthetic scenario; the choice of Vanna-Volga variant matters (model documentation 7.2). Can produce negative prices (F-15). |
| GARCH and GJR-GARCH estimation | Conditional volatility for VaR | V-16, unit tests | Log-likelihood within 1.1e-8 of an independent SLSQP fit and parameters within 1.5e-4 standard errors on 12 simulated series; standard errors within 0.12% of an independent Hessian, but only once both use the same step (F-18). Not compared with the Python `arch` package (it could not be installed). |
| VaR and ES methods | Historical, normal, Cornish-Fisher, delta-gamma, full revaluation, FHS | RISK-REF, V-17, V-18 | Agree with independent Python to 4e-11 relative (186 rows). Delta-normal understates the 99% VaR of short-option positions by 50% to 92% and overstates that of long ones (V-18); Cornish-Fisher delta-gamma can return a negative VaR (F-17). |
| VaR backtests and look-ahead check | Exceptions, Kupiec, independence, traffic light | V-17, V-21, RISK-REF | Statistics agree with independent Python to 4e-15 (40 sequences). A static-volatility control is rejected at both levels; GARCH-normal is rejected on fat-tailed data at 99% and FHS is not. The forecast for day t is bitwise independent of returns from day t on (V-21; a leaky control is caught). One simulated history per innovation type, spot risk factor only. |
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
   command-line layer in week 6, 25 to the smile code in week 7, 28 to the calibration code in week 8 and 15 to the barrier and Vanna-Volga code in week 9 (tables in `docs/test_catalogue.md`).
   All are caught by something except one week-7 change that is equivalent in practice (a rounding-level
   threshold, section 7, L-13) and two week-8 changes that alter speed or an unused bracket and not any result, and, in week 9, the Heston bridge variance (L-17: no exact value to compare with).
   In week 9 two changes survived the first run and led to new unit tests.
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
| BARRIER-REF | Barrier, touch and double no-touch closed forms against independent Python integration | 1e-9 | 700 rows (480 vanillas, 120 touches, 100 double no-touch); worst 6.7e-16 |
| V-12 | Barrier Monte Carlo with Brownian bridge against closed forms | within 3 standard errors; control must fail | 8 contracts all within 3 SE; control (discrete monitoring, 12 steps) overprices a knock-out by 3.8e-3, above 3 SE; Heston with xi = 1e-3 agrees with Black-Scholes closed form; Heston step study reported (0.04687, 0.04639, 0.04640 at 16, 64, 256 steps) |
| V-13 | In-out parity | 1e-12 | 96 contracts; no-touch plus one-touch equals the discount factor (1e-13); shifted-barrier control detected in 79 of 96 (the others have a leg worth under 1e-4) |
| V-14 | Vanna-Volga reproduces the pivots | 1e-11 | 54 cases (6 slices x 3 pivots x 3 variants); control: non-pivot vanilla not reproduced exactly |
| V-15 | Model comparison | reported, not pass/fail | 5 scenarios x 5 contracts; spread 0.003 to 32.8 vol points, 0.002% to 35% of notional (model documentation 7.5); flat-smile control passes |
| RISK-REF | VaR methods and backtest statistics against independent Python | historical, normal, Cornish-Fisher 1e-12 relative; delta-gamma and full revaluation 1e-9 relative or 1e-7 absolute; test statistics and p-values 1e-9 absolute, binomial cdf 1e-12 | 186 VaR rows, worst 4e-11 relative; 40 hit sequences, worst likelihood-ratio error 0, worst p-value error 4e-15 |
| V-16 | GARCH and GJR estimation against independent code | recursion 1e-9 (log-likelihood) and 1e-12 (variance path); log-likelihood within 1e-5 and not lower; parameters within 0.05 SE; standard errors within 5%; asymmetry detected in at least 4 of 5 GJR series; poor-optimiser control must be visible | 12 series: worst log-likelihood gap 1.1e-8, worst parameter difference 1.45e-4 SE, worst SE ratio-1 1.2e-3; GJR detected in 4 of 5 (the fifth, 750 returns, LR 4.7); control passes. Disclosures in the test header: SE step and the 4-of-5 count (F-18) |
| V-17 | VaR backtests of five methods on two simulated series | reported; size and power of the tests; static control must be rejected | 3000 days. Series A: GARCH-normal 31 exceptions at 99% (Kupiec p 0.855), EWMA 40 (0.081), static control 57 (below 0.001). Series B (t6): GARCH-normal 45 (0.010), EWMA 59 (below 0.001), historical 41 (0.056), FHS 34 (0.472). Size at 5%: Kupiec 0.044, independence 0.017; power against a 2% rate 0.985 |
| V-18 | Delta-normal, delta-gamma and full-revaluation VaR for six option portfolios | convexity ordering; short-straddle understatement above 30%; delta-gamma closer than delta-normal; pure-spot control within 0.5% | all assertions hold; 99% one-day VaR: short ATM straddle 1,483 / 2,963 / 2,935 (DN / DG / full), long ATM straddle 1,483 / 206 / 367, short 1M strangle 457 / 1,654 / 1,630; ten-day long straddle: delta-gamma -2,480 against 366 (F-17) |
| V-21 | Look-ahead and leakage check | forecasts up to day t0 bitwise equal when later returns are replaced; leaky control caught | 4 methods x 4 values of t0 pass; the leaky forecaster is caught at 3 of 3 values of t0 |
| ALLOC | No allocation in the path loop | 0 allocations per path; engine allocations independent of path count | passes for GBM, Euler, QE, antithetic and control variate; a model that allocates is detected (control) |

Test counts at version 0.9: 456 in `ctest` (week-10 additions: 42 tests: 4 Nelder-Mead, 8 volatility-model, 5 VaR, 6 backtest and 7 portfolio unit tests, RISK-REF (2), V-16 (4), V-17 (2), V-18 (2), V-21 (2)); Python tests as in 0.8 plus the risk generator's (6). Version 0.8 had 414 in `ctest` (unit, validation, command-line-layer, allocation and two end-to-end tests of
real executables; the week-9 additions are 19 unit tests, BARRIER-REF (1 test over 700 rows), V-12 (4), V-13 (2), V-14 (2) and V-15 (1)); Python tests as in 0.7 plus the barrier generator's. Notes on the numbers:

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
| F-14 | medium | The first version of the barrier Monte Carlo stopped simulating a path as soon as it was knocked out. That is right for a knock-out and for touches, but a knock-in pays the vanilla of the TERMINAL spot, which was then stale. The up-and-in call was priced 11% low and the down-and-in put 0.9% low (5 SE). | V-12 (first run) | Fixed: the early stop is skipped for knock-ins; V-12 passes; a hand mutation reintroducing it is killed. |
| F-15 | medium | Vanna-Volga can return negative prices. On the extreme_skew scenario (risk reversal -8 vol points) the up-and-out call is -0.0062 (unweighted) and -0.0016 (survival), and the double no-touch is -0.594 (unweighted). A negative price is a sign that the adjustment is outside the range where the method is meaningful. | V-15 | Open: the library returns the number and does not clamp it (ADR 0010). Do not use Vanna-Volga for contracts where the smile cost is comparable to the flat price. |
| F-16 | info | The three Vanna-Volga variants disagree by amounts as large as the model spread: for the stressed 2Y one-touch down at 92% the unweighted price is 0.338, the survival 0.420, GK flat 0.532, Heston 0.454. The "Vanna-Volga price" is not unique. | V-15 | Reported (model documentation 7.2); `survival` is the project default. |
| F-17 | medium | Delta-gamma VaR by Cornish-Fisher can be negative or far off for a long-gamma position. For a long ATM 3M straddle the ten-day 99% VaR is -2,480 against 366 by full revaluation, and 206 against 367 at one day. The expansion is not reliable for the strong skew of a quadratic form of a normal. | V-18 | Open. Reported, not corrected; the method is documented as a diagnostic and delta-gamma is never used alone (L-25). |
| F-18 | low | GARCH standard errors depend strongly on the finite-difference step near the stationarity boundary: for series g01 the standard error of beta is 0.0019, 0.0124 and 0.0143 at relative steps 2e-2, 2e-3 and 2e-4, and a first comparison with independent code differed by up to 14% because the two used different steps. | V-16 | Both implementations now use 2e-4 and agree to 0.12%. The error bars are indicative (L-23). |
| F-19 | info | The Christoffersen independence test rejects 1.7% of independent sequences at a 5% level and Kupiec 4.4% (1% exception rate, 2000 sequences): both are conservative with few exceptions. | V-17 | Not a defect. The size is reported and a lower bound on it was dropped from the criteria after the first run (L-24). |
| F-20 | low | A mutant of the Nelder-Mead stop rule (the simplex-size test replaced by `true`) is not detected by any test. The restart loop and the function-value test still stop the search at the same optima on every case in the suite, so the effect, if any, is on iteration counts. | hand mutation check (week 10) | Open. Documented, not investigated further. |

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
* **L-17 (Heston bridge):** the Brownian-bridge crossing probability under Heston uses the average of the
  two end variances over the step. The true crossing probability under stochastic volatility has no closed
  form; the error of the approximation shrinks with the step size and is only measured by a step study (V-12 D,
  prices at 16, 64 and 256 steps differ by 4.8e-4 and 8e-6 on one skewed set). It is not validated against an
  exact value.
* **L-18 (barrier scope):** zero rebate, equally spaced monitoring, constant rates, no fixing calendar,
  no windows, no payment lags; touch options pay at expiry, not at the touch.
* **L-19 (Vanna-Volga):** the flat volatility is the ATM volatility, the pivots are the 25-delta put, the ATM
  call and the 25-delta call, the Greeks are finite differences, and the variant is a convention (F-16). The
  method is not an arbitrage-free model (F-15).
* **L-20 (comparison inputs):** V-15 uses synthetic quotes (L-11) and a Heston fit of 0.005 to 28.5 bp rms
  (worst on extreme_skew, which has two expiries from 3M outwards). The spreads are indicative, one seed, 300,000
  paths; each Heston price has a standard error of 0.0001 to 0.0008 in the table.

* **L-21 (zero mean):** the volatility models assume a zero conditional mean of daily FX returns; no mean is
  estimated. The variance start is the mean of the squared returns of the window.
* **L-22 (boundary optima):** the softmax map keeps persistence below 1, so an optimum on or beyond the boundary
  (integrated GARCH) is approached and never reached; it is not flagged.
* **L-23 (standard errors):** Hessian standard errors are indicative near the stationarity boundary (F-18) and are
  `+infinity` where the Hessian is not positive definite. No profile-likelihood intervals.
* **L-24 (backtest power and size):** with a handful of exceptions the chi-square approximations are poor; the
  independence test is conservative (F-19). Each method has one simulated history per innovation type; V-17 is
  not a measure of which method is best in general. The window is 750 days and GARCH is refitted every 50.
* **L-25 (Cornish-Fisher):** valid only for moderate skewness and excess kurtosis (F-17). Delta-gamma ES is taken from
  a quantile grid of the quadratic, not from the expansion.
* **L-26 (risk factors):** spot only. Volatility, interest rates, theta and the smile are held fixed; options are
  revalued with Garman-Kohlhagen at constant volatility. P&L ten-day figures use the square-root-of-time return
  volatility, not a ten-day model.
* **L-27 (ES):** expected shortfall is computed, not backtested. Data are simulated; no market series was used and
  the Python `arch` package was not run.

## 8. Reproducing these results

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release   # 456 tests
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

**Market risk (0.9).** The estimation code, the five VaR methods and the backtest statistics are fit as tested
components against independent code, on simulated data and for a spot risk factor. They are not fit for
production risk numbers: no market data, no volatility or rate risk factors (L-26), ES not backtested (L-27).
F-17 and F-20 are open; F-18 and F-19 are properties of the methods. The delta-normal VaR of an option portfolio
must not be used for a short-gamma position (V-18).
