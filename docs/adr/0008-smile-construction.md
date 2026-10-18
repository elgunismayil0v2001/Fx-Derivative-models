# ADR 0008: Smile construction from delta quotes

* Status: accepted
* Date: 2026-10-18

## Context

FX options are quoted by delta, not by strike: an at-the-money volatility, a risk reversal and a
butterfly for wing options of a stated delta, at each of a few tenors. Pricing a barrier option
(week 9) or calibrating Heston to a smile (week 8) first needs a volatility at an arbitrary strike.
The plan asks for one explicit delta convention, a smooth simple interpolation, interpolation in total
variance across expiries, and arbitrage checks that report violations.

## Decisions

* **One delta convention, stated and tested.** Premium-excluded delta, spot or forward flavour chosen by
  the caller, ATM defined as the delta-neutral straddle (`K = F exp(sigma^2 T / 2)`). Market practice
  varies by pair and tenor (premium-adjusted delta, ATM forward); supporting several would multiply the
  untested surface. The limitation is written down (L-09) instead of hidden.
* **Strike from delta in closed form** with an inverse normal, not by root finding. The closed form is
  exact and cheap; the independent check is the opposite: the Python reference finds the strikes by root
  finding on the spot-form delta.
* **Simple butterfly.** `BF = (sigma_C + sigma_P)/2 - sigma_ATM`. The market-strangle butterfly needs a root
  find for the wing volatilities and is left out (L-09).
* **The smile is the quadratic in forward log-moneyness through the three pivots.** Alternatives
  considered: (a) Malz, quadratic in delta, which is self-referential (the delta at a strike depends on the
  volatility there) and needs a root find for every query; (b) SVI, which needs five parameters and
  a calibration, so it cannot be fitted to three points; (c) a cubic spline through three points, which
  is the same quadratic with extra freedom. The quadratic is exact at the pivots, has analytic
  derivatives (needed by the density factor) and cannot fail to evaluate. Its cost is stated: it is not
  arbitrage-free by construction, and the checks say where it fails.
* **Surface: linear in total variance at fixed forward log-moneyness, no extrapolation.** The calendar
  condition is checked in the same coordinate, so interpolation adds no violation of its own. An
  out-of-range query returns an error rather than an invented number.
* **Checks report and never repair.** The butterfly report uses Gatheral's density factor; the
  calendar report compares total variance at fixed log-moneyness. Both give counts, the worst value and
  the innermost violation in standard deviations, strike and absolute delta, because "where" is what
  limits use. A repair (for example by projecting onto an arbitrage-free family) would be a model of its
  own and would hide the finding.
* **Grid in standardised log-moneyness** (`x = k / (sigma_ATM sqrt(T))`, default +-4, 801 points), so
  the same grid is meaningful at every expiry. The slack is rounding-level (`1e-12`), not a modelling
  choice.
* **Validation does not trust the density-factor formula.** V-10 compares it with the second difference
  of Garman-Kohlhagen option prices, and requires negative densities to be present in the data so the
  sign check has power. Expectations (flat clean, inverted terms flagged) are derived from theory and fixed
  before the results.

## Consequences

* The smile can be used for any strike with a positive polynomial value; elsewhere `vol_at_strike`
  returns `numerical_failure`.
* The synthetic scenarios show calendar violations in the 5 to 11 delta range for the stressed and emerging
  term structures (finding F-09) and a butterfly violation near the money for an extreme risk reversal
  (F-08). A caller that needs a surface free of both must use a different interpolation (an SVI surface fit
  is a candidate for v1.1) or restrict itself to the range where the checks pass.
* Heston calibration (week 8) is planned to fit the three pivot volatilities per tenor, not the
  interpolated curve, so this limitation would not propagate into the calibrated model.
