# ADR 0011: Market risk: estimation, VaR methods and backtests

* Status: accepted
* Date: 2026-10-21

## Context

Week 10 adds conditional volatility estimation, VaR and ES, and backtests (V-16, V-17, V-18, V-21). Choices:
which optimiser and how parameters are constrained, how VaR methods for option portfolios are defined, how
look-ahead is excluded, and what the evidence can and cannot show.

## Decisions

* **Two new layers, `timeseries` and `risk`, above `numerics` and `pricing`.** `risk` revalues options with the
  Garman-Kohlhagen benchmark and depends on nothing in `mc`, `smile`, `calibration` or `exotics`.
* **Nelder-Mead with restarts, and a softmax map to the constraint region,** instead of a gradient method with
  bounds. The likelihood is cheap, the problem has four parameters, and the map makes every iterate admissible
  (omega > 0, non-negative coefficients, persistence below 1). The cost is that an optimum on the boundary is
  approached and never reached (L-22). The result is checked against an independent SLSQP fit on the constrained
  parameters, which shares no code with it (V-16).
* **Gaussian quasi-likelihood, zero mean.** The same likelihood is maximised for the t(6) series; only the
  innovations differ. A mean is not estimated (L-21).
* **Standard errors from a numerical Hessian with a relative step of 2e-4.** The step matters near the boundary
  (F-18); the same step is used in the C++ and Python code so the comparison tests the implementation and not the step.
  The errors are called indicative.
* **Sign convention: VaR and ES are positive losses; level is the confidence level.** The historical quantile is
  the `ceil(n (1 - level))`-th worst scenario with a 1e-9 guard against `n (1 - level)` landing just above an integer.
* **Delta-gamma by exact moments, not by a second-order Taylor expansion of VaR.** For `Q = c1 w + c2 w^2` the four
  moments are exact, VaR is Cornish-Fisher from them, ES comes from a quantile grid. It is a diagnostic: it fails for
  strong convexity (F-17).
* **Full revaluation on a deterministic normal quantile grid (100,000 points)** instead of random draws, so the
  VaR is reproducible and the comparison with delta-normal has no sampling noise.
* **The backtest passes the forecaster only `returns.subspan(t - window, window)`.** Look-ahead is excluded by the
  type of the call, not by a convention; V-21 tests it by perturbing the future and keeps a leaky control that must be
  caught. A forecaster can still keep state (the GARCH ones refit every N calls) but only ever sees past windows.
* **Backtest statistics are checked against Python that shares no code** (RISK-REF) and the tests' own size
  and power are measured by simulation (V-17), because the chi-square approximations are poor with few exceptions.
* **Data are simulated.** No market series is used: V-16 and V-17 use GARCH simulations with
  known parameters, which is the case where the estimator is expected to work (L-24, L-27).

## Consequences

* Real-data behaviour (jumps, regime changes, holidays) is not evidenced. The code is fit as tested components,
  not as a production risk engine.
* Spot is the only risk factor (L-26). Adding vega or rate risk means a larger scenario type and new V-18 style tests.
* A survivor of the mutation run (F-20) says the Nelder-Mead stop rule is more forgiving than its test suite.
