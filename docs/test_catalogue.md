# Validation test catalogue

Each test has an ID, a pass criterion and a status. A test is `planned` until it exists in
`tests/validation/` as a named test and passes in CI, then it becomes `passing` and gets a link
to its result in `validation_report.md`.

Tolerances are starting points. Changing one needs a reason written in the **Notes** column.

`tests/validation/test_catalogue_consistency.cpp` fails the build if an ID is missing,
duplicated, or outside V-01 to V-21.

| ID | Test | Pass criterion | Planned week | Status | Notes |
|---|---|---|---|---|---|
| V-01 | Put-call parity (Garman-Kohlhagen) | Error below 1e-12 | 2 | passing | `tests/validation/test_v01_put_call_parity.cpp` |
| V-02 | GK Greeks vs finite differences | Relative error below 1e-6 (central difference) | 2 | passing | `tests/validation/test_v02_greeks_fd.cpp`; Richardson-extrapolated stencils for second-order Greeks |
| V-03 | Heston semi-analytic vs GK as vol-of-vol goes to 0 | Prices agree within 1e-6 | 3 | planned | |
| V-04 | Heston semi-analytic robustness over long maturities and extreme parameters | No branch-cut or discontinuity errors over a parameter grid | 3 | planned | |
| V-05 | Heston Monte Carlo (each scheme) vs semi-analytic | Within 3 standard errors; bias vs step size reported | 4 | planned | |
| V-06 | Scheme behaviour when the Feller condition fails | Bias documented for Euler full truncation and QE | 5 | planned | |
| V-07 | Monte Carlo reproducibility | Identical result for 1 and N threads with the same seed | 4 | planned | |
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

## Tests that are not part of the V-series

Unit tests (`tests/unit/`) cover the library's own behaviour: the error type, typed units and
version. They are not validation evidence about the models and are not listed here.
