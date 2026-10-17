# Validation report: FX option pricing models, version 0.5

* **Models in scope:** Garman-Kohlhagen (benchmark), Heston semi-analytic pricer, Monte Carlo engine
  with Euler (full truncation) and Andersen QE schemes, antithetic and control variates.
* **Out of scope for this version:** FX smile construction, Heston calibration, Vanna-Volga and barrier
  models, VaR/ES, exposure and CVA (weeks 7 to 13; their sections will be added to this report).
* **Version tested:** library 0.5.0. The commit is in the metadata of every output file
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
(sections 3 to 5). In short:

* **Garman-Kohlhagen:** constant volatility, deterministic domestic and foreign rates, closed form.
* **Heston:** stochastic variance, `dv = kappa (theta - v) dt + xi sqrt(v) dW`, correlated with the
  spot. Price from one Fourier integral (Lewis 2001) with the numerically stable "little trap"
  characteristic function and adaptive Gauss-Kronrod quadrature.
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
   command-line layer in week 6 (tables in `docs/test_catalogue.md`). All are caught by something. Three
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
| ALLOC | No allocation in the path loop | 0 allocations per path; engine allocations independent of path count | passes for GBM, Euler, QE, antithetic and control variate; a model that allocates is detected (control) |

Test counts at version 0.5: 116 unit, 26 validation, 130 command-line-layer tests, 7 allocation tests and one
end-to-end test of the real executable, 280 in `ctest`; 59 Python tests. Notes on the numbers:

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
  European payoffs on the terminal spot only, no Heston Greeks, no smile, no calibration.
* **L-05 (not yet run):** the GitHub Actions workflows, the Docker build, the vcpkg build and the
  g++-14 and clang-17 builds have not been run. The tests were run with g++ 13 and clang 18 in release,
  debug, AddressSanitizer plus UBSan and ThreadSanitizer builds.
* **L-06 (timings):** throughput numbers come from one 2-core machine and are not asserted by any test.
* **L-07 (fuzzing):** the configuration parser is tested with thousands of randomly damaged documents in
  every build, but libFuzzer targets are not yet in place (planned for weeks 11 to 14).
* **L-08 (shared random numbers):** all options in a run use the same seed, so they share random
  numbers. This is deliberate and documented; it makes price differences smoother and standard errors
  of differences smaller than independent runs would give.

## 8. Reproducing these results

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release   # 280 tests
python -m pytest -q                                                                    # needs numpy, scipy, jsonschema
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
closed and an independent review. Findings F-01 to F-07 are closed or mitigated; no finding is open.
