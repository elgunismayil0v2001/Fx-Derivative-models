# ADR 0010: Barrier pricing, Brownian-bridge Monte Carlo and Vanna-Volga

* Status: accepted
* Date: 2026-10-21

## Context

The library prices barrier and touch options three ways (Garman-Kohlhagen flat volatility, Vanna-Volga, Heston) and
reports the spread as model risk (V-12 to V-15). Choices: where the contract types live, which Vanna-Volga
variant is "the" Vanna-Volga, how discrete monitoring is handled, and what is validated against what.

## Decisions

* **Contract types are in `pricing`, the Vanna-Volga and comparison code in `exotics`.** The layering rule
  (core, numerics, pricing, models, mc, smile, calibration, exotics) forbids `mc` from depending on `exotics`,
  and the Monte Carlo must price the same contracts as the closed form.
* **Two independent routes to each barrier price.** Closed forms (Reiner-Rubinstein blocks; first-passage
  probability; sine series) and conditional Monte Carlo with Brownian-bridge crossing probabilities. Knock-in
  and knock-out come from separate formula tables, so in-out parity (V-13) is a test and not an identity.
* **The closed forms are checked against code that shares nothing with them** (BARRIER-REF): numerical
  integration of killed densities by images, an inverse-Gaussian first-passage integral and a sum of images for
  the double no-touch.
* **Conditional Monte Carlo with the bridge, not a uniform draw per step.** The payoff is multiplied by the
  survival probability, which has a lower variance and no extra random numbers (thread invariance and
  seed behaviour stay those of the engine). The bridge is exact under constant volatility. Under Heston it uses
  the average variance of the step and is an approximation (L-17). Plain discrete monitoring is kept as an
  option so that its bias can be measured (V-12 control).
* **One Vanna-Volga variant is the default, the others are available:** `survival` (adjustment weighted by the
  probability that the contract still lives) is reported as Vanna-Volga; `unweighted` and
  `vanilla_less_knock_in` are kept and shown in V-15. Reason: `survival` is defined for every contract the
  library prices, and the prices of the variants differ by an amount comparable to the model spread (model documentation
  7.2), so hiding the choice would hide model risk. It is a convention, not a validated fact.
* **The Greeks of the pivots and of the contract come from one finite-difference routine**, so a pivot is
  reproduced exactly (V-14) whatever the difference error. The spot step is capped at a quarter of the distance to
  the barrier so a difference never straddles it.
* **The comparison module does not calibrate Heston.** The caller passes parameters and reports the fit
  (V-15 prints the fit error beside the spread).
* **Negative Vanna-Volga prices are returned, not clamped** (F-15), so that the failure is visible.

## Consequences

* The spread in V-15 depends on four stated choices (the flat volatility, the Vanna-Volga variant, the Heston
  fit, the bridge approximation). It is a measure of model dependence, not an error bar.
* Rebates, windows and fixing calendars are out of scope (L-18).
