# ADR 0009: Heston calibration to delta quotes

* Status: accepted
* Date: 2026-10-20

## Context

Week 8 calibrates Heston `(v_0, kappa, theta, xi, rho)` to smile quotes (ATM, 25-delta risk reversal and
butterfly at several expiries) and tests that known parameters are recovered (V-11). Choices had to be
made on the unit of the error, how strikes enter, how bounds are handled, the optimiser, starts, and what
the result must admit about identification.

## Decisions

* **Residuals are Garman-Kohlhagen implied-volatility errors at the market pivot strikes**, equal weights.
  Price errors would be dominated by long-dated ATM options and are not how a desk reads a fit. Equal
  weights are a stated simplification (L-16), not a claim that they are optimal.
* **The strikes are those of the market quotes, fixed during the fit.** The alternative (recompute the delta
  strikes from the model at every step) needs three nested root finds per expiry per residual evaluation.
  The fixed-strike fit is about two orders of magnitude cheaper and, for quotes that come from a Heston model,
  has the same minimum. The nested map exists anyway (`heston_delta_quotes`) because it is the only exact way
  to produce test quotes from parameters, and it is checked against an independent Python implementation.
* **Brent for implied volatility and strike fixed points.** Needs only a bracket, never diverges. Newton on
  vega fails exactly where it is needed (deep wings, small vega). The out-of-the-money option is inverted.
* **Levenberg-Marquardt written in the project (about 250 lines), not a library.** The problem has five
  unknowns, the dependency would be larger than the code, and the behaviour (what counts as converged, how a
  failing evaluation is treated, the standard errors) has to be testable and documented here. It is tested on
  its own against problems with known answers.
* **Bounds by a logistic map**, not projected steps. It keeps every iterate valid and needs no active-set logic;
  the price is that a parameter wanting to leave the box ends near the bound, which the result reports
  (`near_bound`) and which means "not estimated".
* **Feller is not imposed.** Strong smile curvature often needs `xi^2 > 2 kappa theta`.
* **Several starts, all recorded.** One data-driven start plus random starts from counter-based Philox streams
  keyed by `(seed, start)`. The result keeps every start's outcome, so disagreement between starts is visible.
  This reduces the risk of a local minimum; it does not prove a global one (L-16).
* **Identification is reported, not assumed.** Standard errors are `+infinity` when quotes do not outnumber
  parameters or `J'J` is singular. Test V-11 (B) runs the one-expiry case as a control that must fail.
* **Test quotes come from independent Python.** The C++ calibration is fed quotes produced by a different
  Heston pricer, a different integrator and a different root finder, so recovery cannot be an artefact of the
  code agreeing with itself.

## Consequences

* A calibration takes about 0.5 s for five expiries and three starts (one machine, Release).
* Recovery from Heston-generated quotes shows the optimiser and maps are right; it says nothing about whether
  Heston fits a real market (L-15).
* The standard errors are linearised and in the noise study were up to a third too small (L-14); they are
  indicative, not a confidence interval.
* The calibration uses three quotes per expiry at one delta. Adding 10-delta quotes would need the smile
  construction to change first (L-10, L-11).
