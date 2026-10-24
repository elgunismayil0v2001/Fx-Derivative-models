# ADR 0004: Heston pricing method

* Status: accepted
* Date: 2026-10-12

## Context

The library needs a semi-analytic Heston price that is accurate, fast enough to sit inside a
calibration loop, and safe: it must never return a plausible but wrong number. Three
choices matter: the characteristic-function formulation, the way the price is obtained from it,
and the integration routine.

## Decision

* **Formulation 2 of Albrecher et al.** (`e^{-d tau}`, `g = (a - d) / (a + d)`), rearranged so
  nothing is divided by `xi^2`. The original 1993 form has branch-cut jumps at long maturities.
  The rearrangement also makes `xi = 0` exact, which gives V-03 a clean limit.
* **Lewis (2001) single integral** on the line `Im(u) = -1/2`, not the two probabilities of
  Heston's paper. One integral instead of two, a decaying non-singular integrand, and put-call
  parity is built in. The two-integral form is kept in Python as the independent reference.
* **Own adaptive Gauss-Kronrod routine** (`numerics/quadrature.hpp`, no new dependency). Small,
  tested on integrals with known answers, and its failure mode is an explicit `converged = false`
  that the pricer turns into a `not_converged` error.
* **Out-of-the-money option is priced directly**, the in-the-money one by parity, to avoid
  cancellation of two large numbers.
* **Every failure is an error:** non-finite value, no-arbitrage bound violated, quadrature not
  converged.

## Alternatives considered

* Carr-Madan FFT: efficient for many strikes at one expiry, but needs a damping parameter and
  interpolation, and gives a grid of prices, not one accurate price. Revisit if calibration needs speed.
* Cos method (Fang and Oosterlee): fast and accurate for smooth densities; less transparent to
  validate. Revisit with FFT.
* Heston's original formulation: rejected for the branch-cut problem.

## Consequences

* V-03 and V-04 can state sharp, checkable criteria (order of convergence, jump size).
* A control experiment in V-04 reruns the original formulation and requires that the jump is
  detected, so the check cannot silently become vacuous.
* Absolute accuracy about `1e-13 F`; deep out-of-the-money prices have no relative accuracy.
* Cost 0.1 to 0.3 ms per price (Release), fine for calibration, not for pricing millions of
  options per second.
