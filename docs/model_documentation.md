# Model documentation

Status: **draft, week 8.** Sections marked *planned* are placeholders that name the week in
which they are written. Garman-Kohlhagen is implemented and checked by V-01 and V-02. The Heston
semi-analytic pricer is implemented and checked by V-03, V-04 and an independent Python table.

| Section | Status |
|---|---|
| 1. Purpose and scope | draft |
| 2. Notation and conventions | draft |
| 3. Garman-Kohlhagen (benchmark) | implemented; V-01 and V-02 passing |
| 4. Heston | semi-analytic pricer (V-03, V-04, reference table, week 3); calibration to smile quotes (V-11, CAL-REF, week 8, sections 4.6 to 4.11) |
| 5. Monte Carlo schemes | planned, weeks 4-5 |
| 6. FX smile from delta quotes | implemented; V-09, V-10, SMILE-REF passing (week 7) |
| 7. Vanna-Volga | planned, week 9 |
| 8. Barrier options | planned, week 9 |
| 9. Market risk (VaR, ES, backtests) | planned, week 10 |
| 10. Counterparty credit risk (EE, PFE, CVA) | planned, week 11 |
| 11. Limitations | running list |
| 12. References | running list |

## 1. Purpose and scope

The library prices FX options and measures market and counterparty risk for a small FX
portfolio. Its main purpose is to serve as an **independent benchmark and testing framework**:
each model is checked against an exact or independent reference, and the differences between
models are measured and reported (see `validation_report.md` when it exists).

Out of scope for v1.0: real-time data, execution, equity and commodity models, a full
local-stochastic-volatility model. See `ROADMAP.md`.

## 2. Notation and conventions

* `S_t` is the spot exchange rate quoted as **domestic currency per one unit of foreign
  currency**. For EUR/PLN, PLN is domestic and EUR is foreign.
* `r_d`, `r_f` are continuously compounded domestic and foreign interest rates. They are
  deterministic and constant in the first version of each model.
* `T` is the time to expiry in years. The day-count convention is **ACT/365 fixed**:
  `(end - start)` calendar days divided by 365, including in leap years. Implemented as
  `fxval::act365_fixed`.
* `sigma` and all volatilities are **decimals** (0.08 means 8%). The typed units in
  `include/fxval/core/units.hpp` enforce this.
* `W` denotes a Brownian motion under the domestic risk-neutral measure `Q_d`, unless a measure
  is named explicitly. `N` and `phi` are the standard normal cumulative distribution and density.
* `K` is the strike, quoted like `S`.

## 3. Garman-Kohlhagen (benchmark model)

### 3.1 Dynamics under the domestic risk-neutral measure

    dS_t = (r_d - r_f) S_t dt + sigma S_t dW_t

Why the drift is `r_d - r_f`: one unit of foreign currency held in a foreign deposit grows to
`e^{r_f t}` units. In domestic currency that traded asset is worth `S_t e^{r_f t}`. Under `Q_d`
every traded asset, discounted at the domestic rate, is a martingale, so `S_t e^{r_f t}` must
have drift `r_d`. Applying Ito's lemma to `S_t e^{r_f t}` gives the drift `r_d - r_f` for `S_t`.

### 3.2 Solution and forward

    S_T = S_0 exp( (r_d - r_f - sigma^2 / 2) T + sigma W_T )
    F   = E[S_T] = S_0 exp( (r_d - r_f) T )

`F` is the fair forward rate (covered interest parity).

### 3.3 European option prices

With `d1 = [ ln(S/K) + (r_d - r_f + sigma^2/2) T ] / (sigma sqrt(T))` and `d2 = d1 - sigma sqrt(T)`:

    Call = S e^{-r_f T} N(d1) - K e^{-r_d T} N(d2)
    Put  = K e^{-r_d T} N(-d2) - S e^{-r_f T} N(-d1)

Equivalent Black form: `Call = e^{-r_d T} [ F N(d1) - K N(d2) ]`, with
`d1 = [ ln(F/K) + sigma^2 T / 2 ] / (sigma sqrt(T))`.

### 3.4 Put-call parity (test V-01)

    Call - Put = S e^{-r_f T} - K e^{-r_d T}

### 3.5 Greeks (checked against finite differences in test V-02)

| Greek | Formula |
|---|---|
| Spot delta | `e^{-r_f T} N(d1)` (call) |
| Forward delta | `N(d1)` (call) |
| Gamma | `e^{-r_f T} phi(d1) / (S sigma sqrt(T))` |
| Vega | `S e^{-r_f T} phi(d1) sqrt(T)` |
| Vanna (`d Delta / d sigma`) | `-e^{-r_f T} phi(d1) d2 / sigma` |
| Volga (`d Vega / d sigma`) | `Vega * d1 d2 / sigma` |
| Theta (`dV/dt`) | Call: `-S e^{-r_f T} phi(d1) sigma / (2 sqrt(T)) + r_f S e^{-r_f T} N(d1) - r_d K e^{-r_d T} N(d2)` |
| Rho domestic (`dV/dr_d`) | Call: `K T e^{-r_d T} N(d2)`; put: `-K T e^{-r_d T} N(-d2)` |
| Rho foreign (`dV/dr_f`) | Call: `-S T e^{-r_f T} N(d1)`; put: `S T e^{-r_f T} N(-d1)` |

Put theta replaces `N(d1)` / `N(d2)` with `-N(-d1)` / `-N(-d2)` on the carry terms and keeps the
common first term. The formulas were derived by hand (for vanna, `d d1 / d sigma = -d2 / sigma`);
V-02 confirms them against finite differences, and an independent Python table confirms them again.

### 3.6 Pricing PDE

The value `V(t, S)` of a European claim satisfies

    dV/dt + (1/2) sigma^2 S^2 d2V/dS2 + (r_d - r_f) S dV/dS - r_d V = 0

with the payoff as terminal condition. This is the link to Feynman-Kac used in the Monte Carlo
tests: the discounted expected payoff under `Q_d` solves this PDE.

### 3.7 Degenerate cases

* `sigma sqrt(T) = 0`: the **price** is the discounted intrinsic value of the forward,
  `e^{-r_d T} max(w (F - K), 0)`; at `T = 0` it is the intrinsic value of spot.
* The **Greeks** are not defined there (they divide by `sigma sqrt(T)`; gamma and vega at the money
  are infinite) and `garman_kohlhagen_greeks` returns `ErrorCode::invalid_argument`. Returning
  zeros would look like a valid answer (ADR 0001).
* A non-finite result (overflow in `exp`) returns `ErrorCode::numerical_failure`.

### 3.8 Assumptions and known limitations

* Constant volatility: it produces no smile or skew, so it misprices options away from the money
  relative to the market. This is the reason the project compares it with Heston and Vanna-Volga.
* Deterministic, constant interest rates in both currencies.
* Continuous trading without transaction costs, and no jumps.

## 4. Heston

### 4.1 Dynamics and parameters

Under `Q_d`:

    dS_t = (r_d - r_f) S_t dt + sqrt(v_t) S_t dW^1_t
    dv_t = kappa (theta - v_t) dt + xi sqrt(v_t) dW^2_t,    d<W^1, W^2>_t = rho dt

| Parameter | Meaning | Main effect on the smile | Domain in `HestonParams` |
|---|---|---|---|
| `v_0` | initial variance (not volatility) | level of the short-dated smile | `[0, 25]` |
| `kappa` | speed of mean reversion of variance | how fast the term structure moves from `v_0` to `theta` | `(0, 100]` |
| `theta` | long-run variance | level of the long-dated smile | `(0, 25]` |
| `xi` | volatility of variance | curvature of the smile | `[0, 10]` |
| `rho` | spot-variance correlation | skew (asymmetry) | `[-1, 1]` |

Feller condition: `2 kappa theta >= xi^2` keeps the variance process strictly positive. The
semi-analytic price does **not** need it (the characteristic function is valid either way, and
V-04 includes Feller violations); simulation schemes do (V-06 measures how).

`xi = 0` is allowed: the variance is then deterministic, `v(t) = theta + (v_0 - theta) e^{-kappa t}`,
and the model is Garman-Kohlhagen with total variance `W = E[integral of v] =
theta tau + (v_0 - theta)(1 - e^{-kappa tau}) / kappa` (`expected_integrated_variance`).

### 4.2 Characteristic function

Let `X = ln(S_tau / F)`, which does not depend on the interest rates. Then
`dX = -(v/2) dt + sqrt(v) dW^1` and `phi(u; tau) = E[e^{iuX}] = exp(C(tau) + D(tau) v_0)`, where
with `h = i u + u^2` the functions solve the Riccati equations (Feynman-Kac)

    D' = (xi^2 / 2) D^2 + (rho xi i u - kappa) D - h / 2,     C' = kappa theta D,     D(0) = C(0) = 0.

The closed-form solution, in the "formulation 2" of Albrecher, Mayer, Schoutens and Tistaert
(*the little Heston trap*), with `a = kappa - rho xi i u` and `d = sqrt(a^2 + xi^2 h)` (principal
root), `g = (a - d) / (a + d)`:

    D = -h / (a + d) * (1 - e^{-d tau}) / (1 - g e^{-d tau})
    C = (kappa theta / xi^2) [ (a - d) tau - 2 ln( (1 - g e^{-d tau}) / (1 - g) ) ]

Why this form and not the one in Heston's paper: the original paper uses `e^{+d tau}` and
`g = (a + d) / (a - d)`. For long maturities the complex logarithm in `C` then jumps by `2 pi i`
and the price is wrong with no warning. Test V-04 shows the jump in the original form
(`kappa = 0.3, theta = v_0 = 0.04, xi = 0.5, rho = -0.9`: a relative step of 0.59 near `tau = 16`) and none in this one.

Implementation details that matter (`src/heston.cpp`):

* **No division by `xi^2`.** Since `a^2 - d^2 = -xi^2 h`, the quantities `(a - d) / xi^2 = -h / (a + d)`
  and `g / xi^2 = -h / (a + d)^2` are computed directly. The textbook form loses about
  `eps / xi^2` of relative accuracy, which is fatal for small `xi`; this one is exact at `xi = 0`.
* `ln(1 + z) / z` uses a series for `|z| < 1e-3`.
* **`a + d` without cancellation.** If `Re(a) < 0` (`rho xi > kappa`) then `d` is close to `-a`;
  `a + d` is computed as `xi^2 h / (d - a)`. Found by test V-04: before this, `phi(-i)` returned NaN.
* `u = 0` and `u = -i` are removable singularities (`h = 0`); the function returns exactly 1
  there (total probability; martingale property `E[S_T] = F`).

### 4.3 European price (Lewis 2001)

With `x = ln(F / K)`:

    E[(F e^X - K)^+] = F - (sqrt(F K) / pi) * Integral_0^inf Re[ e^{i u x} phi(u - i/2) ] / (u^2 + 1/4) du

and `E[(K - F e^X)^+] = K - (the same integral term)`. The price is the discounted
expectation, `e^{-r_d tau} * (...)`. One integral instead of the two probabilities of Heston's
paper; the integrand is smooth and decays quickly. Convergence on the line `Im(u) = -1/2` needs
only the moment of order 1/2 of `S_T`, which is always finite.

Out-of-the-money option: `df * (min(F, K) - I)`, so the cancellation is against a small number.
In-the-money option: out-of-the-money value plus `df |F - K|` (parity).

### 4.4 Numerical method and accuracy

* `[0, inf)` is mapped to `[0, 1)` with `u = t / (1 - t)`, then integrated with adaptive
  Gauss-Kronrod (7, 15) in the style of QUADPACK `qag` (`fxval/numerics/quadrature.hpp`, tested on
  its own): start from 8 panels, bisect the panel with
  the largest error estimate, at most 4000 panels.
* Target: absolute price error about `1e-14 F`, relaxed to the rounding level of the sum
  (`10 eps` times the integral of `|f|`) when the target is below what floating point can give.
  Failure to converge returns `ErrorCode::not_converged`; a non-finite value returns
  `numerical_failure`. A price violating the no-arbitrage bounds also returns `numerical_failure`.
* Measured: against an independent Python implementation, worst absolute difference 1.6e-12 on
  589 call and put prices (`tests/validation/data/heston_reference.csv`). At `xi = 0` the
  difference from Garman-Kohlhagen is 4e-16.
* Cost: about 0.1 to 0.3 ms per price in a Release build.

### 4.5 Limitations

* Deep out-of-the-money prices have absolute accuracy about `1e-13 F`, not relative accuracy.
* Parameters are constant in time; one parameter set is fitted to all expiries (calibration, section 4.7).
* Interest rates are deterministic (as in the rest of v1.0).
* The `not_converged` path is not exercised by a test: no input is known that triggers it.
* Not covered yet: Heston Greeks (by finite differences of the price) and exotic payoffs (week 9 by Monte Carlo). Implied volatility output and calibration are in sections 4.6 to 4.11.

### 4.6 Heston in market units (week 8)

Code: `include/fxval/pricing/implied_volatility.hpp`, `include/fxval/numerics/brent.hpp`,
`include/fxval/calibration/heston_smile.hpp`. Decisions: `docs/adr/0009-heston-calibration.md`.

FX markets quote Garman-Kohlhagen implied volatilities at delta strikes, so a calibration compares
models in that unit. Two maps are needed.

**Price to implied volatility.** For a strike `K` and forward `F`, the out-of-the-money option (a call if
`K >= F`, otherwise a put) is priced and inverted with Brent's method on `sigma` in `[1e-6, 5]`. An
in-the-money price would be converted to the out-of-the-money one by put-call parity, which is exact, so that the
price carries the volatility information at full relative precision (vega is tiny deep in the money). Prices
outside the no-arbitrage bounds, a zero expiry and volatilities outside the search range are errors, never NaN.
Brent's method (bisection safeguarded by inverse quadratic interpolation and secant steps) is used because it
cannot leave the bracket and cannot diverge; it is called thousands of times per calibration.

**Heston parameters to delta quotes.** The strike of a 25-delta option depends on the volatility *at that
strike*, so each quote is a fixed point `sigma = IV_Heston(K(sigma))`, with `K(sigma)` from
`smile::strike_from_delta` or `smile::atm_strike` (section 6.2). It is solved by Brent on
`f(sigma) = IV_Heston(K(sigma)) - sigma`. The bracket starts at `0.3` times the model volatility level
`sqrt(E[integral of v] / tau)` and its upper end is found by scanning `1.25, 1.6, 2.2, 3.0` times that level
and stopping at the first sign change. Starting at five times the level, as the first version did, fails: a
25-delta call at `sigma = 1.08`, `T = 2` is eight forwards away and the Heston price underflows (finding F-11).
The result is `(ATM, RR, BF)` in the simple-butterfly definition of section 6.1, so that `SmileSlice` built from
them puts its pivot strikes exactly where the model has those deltas (unit test).

### 4.7 The calibration problem

Unknowns `p = (v_0, kappa, theta, xi, rho)`. Data: for each expiry `T_j`, the three pivots (25-delta put, ATM,
25-delta call) at the strikes `K_{j,P} < K_{j,A} < K_{j,C}` that `SmileSlice` derives from the quotes, with
market volatilities `sigma^mkt_{j,i}` (section 6.3). The residual vector has one entry per pivot:

    r_{j,i}(p) = IV_Heston(K_{j,i}; T_j, p) - sigma^mkt_{j,i}.

* **Volatility errors, not price errors.** Prices are dominated by long-dated at-the-money options; an error
  in volatility points is comparable across strikes and expiries and is the unit in which a desk reads a fit.
  All residuals have equal weight (no vega or expiry weighting, limitation L-16).
* **Strikes are fixed.** The strikes come from the market quotes and are not recomputed from the model during the
  fit; this avoids the nested root finds of section 4.6 inside the optimiser (a calibration costs about 0.5 s
  instead of minutes). When the quotes come from a Heston model the two agree exactly, which is why V-11 recovers
  the parameters to rounding level.
* **Bounds** (defaults, adjustable): `v_0, theta` in `[1e-4, 1]`, `kappa` in `[0.05, 25]`, `xi` in `[0.02, 4]`,
  `rho` in `[-0.98, 0.98]`. They keep the optimiser out of regions where the pricer is not meant to be used and
  do not encode a view of the market.
* **Feller is not imposed.** FX smiles with strong curvature often need `xi^2 > 2 kappa theta`; forcing the
  condition would bias exactly those fits. The result reports `params.feller_satisfied()`.
* **Starts.** One data-driven start (`v_0` from the shortest ATM, `theta` from the longest ATM, `kappa = 1.5`,
  `xi = 0.5`, `rho = clamp(4 RR / ATM)`) plus a number of random starts (`random_starts`, default 6) drawn from
  counter-based Philox streams keyed by `(seed, start number)`, so a calibration is reproducible and independent of
  anything else that consumed random numbers. All starts are run and recorded (`starts`); the best converged one,
  by cost, is returned.

### 4.8 The optimiser (`fxval/numerics/least_squares.hpp`)

Levenberg-Marquardt on `C(x) = (1/2) sum r_i^2`:

    (J'J + lambda D) delta = -J'r,       D = diag(J'J) (Marquardt 1963 scaling, floored),   x <- x + delta.

`J` is a central finite-difference Jacobian (step `1e-7 max(|x|, 1)`); the Heston price is smooth enough that this
costs about `1e-9` of noise. `lambda` follows Nielsen (1999): after an accepted step it is multiplied by
`max(1/3, 1 - (2 rho - 1)^3)`, `rho` being the ratio of the actual to the predicted decrease; after a rejected one
by `nu`, which doubles each time. A step into a region where the pricer returns an error counts as rejected.
Bounds are handled by a logistic map between an unbounded variable `u` and the parameter in `(lo, hi)`. The
cost is that a parameter whose true value is at or beyond a bound drifts to `u -> infinity` and its gradient
vanishes, so the run stops with the parameter within `0.1%` of the range of the bound: this is reported as
`near_bound` and means "the data want a value outside the box", not an estimate.

Termination: `gradient_small`, `step_small`, `cost_small` count as converged; `max_iterations` and `stalled` do
not, and `converged` is false in the result.

### 4.9 Identifiability and standard errors

A single expiry provides three quotes for five unknowns: there are infinitely many parameter sets that fit it
exactly, so a fit can be excellent and the parameters arbitrary. Test V-11 (B) is the control for this. With
`N` quotes and `p = 5` parameters the standard errors are `sqrt(s^2 [(J'J)^{-1}]_jj)` with
`s^2 = 2 C / (N - p)` and `J` the Jacobian in the natural parameters at the solution. They are `+infinity` when
`N <= p` or `J'J` is numerically singular, and no finite number is ever reported in that case. They are linearised
estimates (limitation L-14): V-11 (C) compares them with the actual spread of repeated noisy calibrations.

### 4.10 Evidence

* **CAL-REF** (`tests/validation/test_calibration_reference.cpp`, tables written by `python/gen_calibration_reference.py`).
  Independent Python: textbook `P1/P2` Heston prices with QUADPACK, `brentq` for implied volatility, nested `brentq` for
  delta strikes. 160 implied volatilities, worst difference 2.1e-13; 120 sets of quotes (24 parameter sets times five
  expiries), worst difference 7.5e-15 (criterion 1e-8). It found the defect described in 4.6 (F-11).
* **V-11 (A), exact quotes.** 24 parameter sets (16 satisfy Feller, 8 violate it; 12 spot delta, 12 forward delta),
  quotes from the Python table at 1M, 3M, 6M, 1Y, 2Y. All 24 recovered: worst volatility error 8.3e-12 (criterion
  1e-6), worst parameter error 3.7e-9 (criterion 1e-4), all converged.
* **V-11 (B), one-expiry control.** The 1Y expiry only: all 24 fits reproduce the three quotes (worst error 4.3e-11)
  and in 24 of 24 sets at least one parameter is off by more than 1%; all 24 report infinite standard errors.
* **V-11 (C), noise.** Three sets, 40 noisy copies each, noise 5e-4 on every pivot volatility. Mean `rms^2` was 0.86,
  0.94 and 0.85 times the value linear theory predicts, `sigma^2 (N - p) / N` (criterion within 25%). The ratio of the
  observed spread of each parameter to its reported standard error was between 0.85 and 1.50 (criterion 0.6 to 1.6). On
  set 0 it was 1.19 to 1.50 and on set 16 1.06 to 1.30, so there the reported errors are too small by up to a third
  (limitation L-14); on set 8 the ratio is 0.85 to 0.99. The mean estimate was within 4 standard errors of the truth for all 15 parameter checks; the largest
  was `rho` on set 16 at 3.45, followed by `xi` 2.61 and `theta` 2.22, which suggests a small estimator bias at this
  noise level for that set (finding F-13; L-14).
* Unit tests, including an exact property that no fitting code knows about: swapping the two currencies maps a
  Heston model to the Heston model with `rho' = -rho`, `kappa' = kappa - rho xi`, `theta' = kappa theta / kappa'`
  (same `v_0`, `xi`), and the calibration recovers parameters related by that map from the swapped market.
* 28 hand mutations of the week-8 code: 26 caught, 2 survive (they change speed, or a bracket that is never needed,
  not any result); two are seen only by V-11 (C) (test catalogue).

### 4.11 Limitations of the calibration

L-14 the standard errors are linearised and, as V-11 (C) shows (observed spread up to 1.5 times the reported error), can be too small by up to a third; L-15 the
test quotes are generated by the same model family that is fitted ("inverse crime"): recovery shows the optimiser
and the maps are right, not that Heston can fit a real smile; L-16 three quotes per expiry at one wing delta,
equal weights, no regularisation and no Feller constraint; several starts reduce the chance of a local minimum but
do not prove the global one. Run time is about 0.5 s per calibration (five expiries, three starts) on the machine
used.

## 5. Monte Carlo

Status: the engine, exact GBM and the Heston Euler scheme with full truncation (week 4); the QE
scheme, antithetic and control variates, the Feller study and the scaling benchmark (week 5).

### 5.1 Estimator and standard error

For a payoff `g` of the terminal spot, the price is estimated by

```
price_hat = e^{-r_d T} * (1/N) * sum_i g(S_T^(i)),
SE        = e^{-r_d T} * s / sqrt(N),     s^2 = sample variance of g(S_T^(i)).
```

The standard error measures sampling noise only. It says nothing about the bias of a time
discretisation, which is why test V-05 compares with the semi-analytic price and reports the
bias separately.

### 5.2 Random numbers

* **Philox4x32-10**, a counter-based generator (Salmon et al., 2011): the output is a function of
  (key, counter), with no state to share. Key = 64-bit seed. Counter = (block number, path
  number). **Path number = stream number**, so path `p` gets the same numbers whichever thread
  runs it. Checked against the published known-answer vectors.
* One 128-bit block gives two uniforms in `(0, 1)` (top 53 bits of each 64-bit half,
  `u = (k + 1/2) 2^-53`), and Box-Muller turns them into two independent standard normals. The
  largest possible `|z|` is about 8.6; real normals exceed that with probability below `1e-17`.
* A Heston step uses exactly one block (`Z_v`, `Z_perp`).

### 5.3 Models

* **GBM** (the Garman-Kohlhagen dynamics) is simulated exactly:
  `S_T = S_0 exp((r_d - r_f - sigma^2/2) T + sigma sqrt(T) Z)`. The step count is ignored. It
  exists to validate the engine and the discounting against the closed form.
* **Heston, Euler with full truncation** (Lord, Koekkoek and van Dijk, 2010). With `dt = T / n`,
  `v+ = max(v, 0)` and `Z_S = rho Z_v + sqrt(1 - rho^2) Z_perp`:

  ```
  ln S_{k+1} = ln S_k + (r_d - r_f - v+/2) dt + sqrt(v+ dt) Z_S
  v_{k+1}    = v_k + kappa (theta - v+) dt + xi sqrt(v+ dt) Z_v
  ```

  `S` is advanced in logs, so it stays positive; the raw `v` may become negative and is carried
  (not reset), which is what "full" truncation means. Every step multiplies `S` by
  `exp(-v+ dt/2 + sqrt(v+ dt) Z_S)`, whose conditional mean is 1, so `E[S_T] = F` exactly for any
  number of steps (a unit test checks this). The bias is in the distribution, not the mean.

### 5.4 Parallel execution and reproducibility

Paths are grouped in fixed blocks (default 4096). Threads take blocks from an atomic counter;
each block is summarised on its own (Welford mean and sum of squares); the block summaries are
merged in **block order** (Chan, Golub, LeVeque). Which thread ran which block cannot change a
bit of the result, so the same seed gives bitwise identical output for 1 or N threads (V-07).
Changing the *block size* changes the order of the arithmetic and therefore the last digits (not
the distribution); this is documented behaviour and is checked to `1e-12` relative.

### 5.5 Evidence (week 4)

* **V-05** (`test_v05_heston_mc_vs_analytic.cpp`), seed 20261014, fixed before looking at results:
  GBM engine vs Garman-Kohlhagen, 16 comparisons (8 markets, call and put, 200,000 paths): worst
  |z| = 2.20, all within 3 SE. Heston Euler, Feller satisfied (`v0 = theta = 0.04`, `kappa = 2`,
  `xi = 0.3`, `rho = -0.5`, `T = 1`), 64 steps, 200,000 paths, strikes 0.95, 1.10, 1.25 at spot
  1.10: worst |z| = 1.69. Bias vs steps for a set that violates Feller badly (`kappa = 0.5`,
  `xi = 1`, `rho = -0.9`, `T = 2`, at the money, 100,000 paths): bias 3.4e-2, 1.9e-2, 9.2e-3,
  4.2e-3, 1.7e-3 for 8, 16, 32, 64, 128 steps (86, 61, 36, 18, 7.8 standard errors).
* **V-07** (`test_v07_mc_reproducibility.cpp`): bitwise identical mean, standard error and
  variance for 1, 2, 3, 4, 7, 8, 16 threads and hardware concurrency, for GBM and Heston; 25 repeated
  runs with 8 threads identical; a naive "equal share per thread" reduction, run as a control,
  differs from its 1-thread result for 4 of 4 thread counts, so the bitwise check can fail.
* **MC-PATH-REF** (`test_mc_path_reference.cpp`): 120 (seed, stream) cases, including 64-bit seeds
  and streams, `xi = 0`, `rho = +-1`, one step, against an independent Python implementation of
  Philox, Box-Muller and the path. Worst differences on the development machine: 0 (bitwise);
  the tolerance (1e-13 for normals, 1e-9 for paths) allows for other libm versions.

### 5.6 Heston QE scheme (week 5)

Andersen (2008), `include/fxval/mc/heston_qe.hpp`, decision record ADR 0006. The variance is a
square-root process; instead of a truncated Euler step, QE samples `v_{n+1}` from a distribution
that has the exact conditional mean and variance of the true transition. With `e = exp(-kappa dt)`:

```
m   = theta + (v - theta) e
s2  = v xi^2 e (1 - e) / kappa + theta xi^2 (1 - e)^2 / (2 kappa)
psi = s2 / m^2
```

* `psi <= psi_c = 1.5` (quadratic): `v' = a (b + Z)^2`, `b^2 = 2/psi - 1 + sqrt(2/psi) sqrt(2/psi - 1)`,
  `a = m / (1 + b^2)`, `Z ~ N(0, 1)`.
* `psi > psi_c` (exponential with a point mass at zero): `p = (psi - 1)/(psi + 1)`,
  `beta = (1 - p)/m`; `v' = 0` if `U <= p`, otherwise `v' = ln((1 - p)/(1 - U)) / beta`.
  Here `U = Phi(Z_v)` (`erfc`), so one Philox block still gives all the randomness of a step.

Both branches reproduce `m` and `s2` exactly (unit tests check the sample mean and variance of the
variance step against these formulas in each branch, and the size of the point mass at zero).
The spot uses the central discretisation (`gamma1 = gamma2 = 1/2`):

```
ln S' = ln S + (r_d - r_f) dt + K0 + K1 v + K2 v' + sqrt(K3 v + K4 v') Z_perp
K1 = dt/2 (kappa rho/xi - 1/2) - rho/xi    K2 = dt/2 (kappa rho/xi - 1/2) + rho/xi
K3 = K4 = dt/2 (1 - rho^2)                 K0 = -ln M - (K1 + K3/2) v,  M = E[exp(A v') | v],  A = K2 + K4/2
```

`M` has a closed form in each branch (quadratic: `exp(A b^2 a / (1 - 2 A a)) / sqrt(1 - 2 A a)`,
exponential: `p + beta (1 - p) / (beta - A)`), so `K0` makes `E[S' | S, v] = S e^{(r_d - r_f) dt}`
exactly: `E[S_T]` equals the forward for any number of steps. `M` exists only if `2 A a < 1` or
`A < beta`; when it does not (steps of several years with large positive `rho`) the uncorrected
`K0 = -rho kappa theta dt / xi` is used. `xi > 0` is required.

### 5.7 Variance reduction (week 5)

`include/fxval/mc/variance_reduction.hpp`; all estimators are built on `run_samples`, so they are
reproducible for any thread count (unit tests repeat the V-07 check for them).

* **Antithetic variates.** Each sample averages a path with its mirror image driven by `-z`.
  `McConfig.paths` counts pairs; one pair costs two path simulations, so the fair comparison is plain
  with `2n` paths against `n` pairs: ratio `Var(plain) / (2 Var(pair))`.
* **Control variate.** `f(S) - c (g(S) - mu)` with `mu = E[g]` known exactly. Any fixed `c` is
  unbiased; `c* = Cov(f, g) / Var(g)` minimises the variance, which falls by `1 - corr(f, g)^2`.
  `c` comes from a pilot run on other streams (`pilot_stream_offset`), not from the main paths.
  For Heston the control is `g(S_T) = S_T`, `mu` = forward. Under GBM any Garman-Kohlhagen price is
  a valid control (V-08 d). **A GK price is not a valid control for Heston paths.** A wrong `mu` is
  invisible in the standard error; only a comparison with an exact price shows it.

### 5.8 Evidence (week 5)

All runs: seed 20261016 (fixed before the results), Release build, 2 threads, results recomputed
by `ctest`; Debug and sanitizer builds use fewer paths.

* **QE-PATH-REF** (`test_mc_qe_path_reference.cpp`): 160 cases (44 antithetic, 8 that take the
  fallback, both branches 1376 and 1155 times), worst relative difference to the independent Python
  implementation 3.0e-15 (spot) and 8.7e-16 (variance, absolute). Same-author caveat in ADR 0006.
* **V-06** (`test_v06_feller_study.cpp`): four parameter sets that violate Feller (ratio 0.03 to 0.1),
  three strikes, 4 to 64 steps, 100,000 paths, both schemes on the same random numbers. At the money
  (bias in price units, MC minus semi-analytic; QE / Euler):

  | set | 4 steps | 8 steps | 16 steps | 32 steps | 64 steps |
  |---|---|---|---|---|---|
  | A (`kappa 0.5, xi 1.0, rho -0.9, T 2`) | -4.2e-4 / 5.5e-2 | -7.0e-5 / 3.5e-2 | -5.6e-6 / 1.9e-2 | -2.1e-4 / 9.6e-3 | 1.4e-4 / 4.6e-3 |
  | B (`kappa 1.0, xi 1.5, rho -0.7, T 1`) | -3.3e-4 / 4.0e-2 | 6.4e-5 / 2.5e-2 | 3.1e-4 / 1.3e-2 | -6.8e-5 / 5.6e-3 | 5.1e-4 / 2.5e-3 |
  | C (`kappa 0.3, xi 0.9, rho -0.3, T 3`) | -1.1e-3 / 6.3e-2 | -2.2e-4 / 4.6e-2 | 3.0e-4 / 2.8e-2 | -5.9e-4 / 1.5e-2 | 3.6e-4 / 7.9e-3 |
  | D (`kappa 0.8, xi 0.8, rho +0.5, T 1`) | 8.0e-4 / 2.2e-2 | 1.0e-4 / 1.4e-2 | -7.6e-4 / 6.5e-3 | -1.3e-4 / 2.4e-3 | -5.3e-5 / 1.3e-3 |

  QE is closer than Euler in every set, strike and step count up to 16 (36 of 36 comparisons), and
  within 4 standard errors of the exact price at 32 steps for all 12 set-strike pairs (worst 1.16).
  QE's differences from the exact price are at the level of sampling noise (standard error about
  2e-4 to 7e-4 here), so the table does not show a QE bias, and it cannot rule out one below that
  level. Euler at 32 steps is 37, 13, 17 and 3.0 standard errors away at the money (power check).
  The terminal spot is a martingale for QE at 16 steps (z = 1.3, 0.8, 0.9, -1.3 for the four sets;
  Euler 0.6, 1.8, 0.5, -0.1). QE is insensitive to `psi_c`: -0.11, -0.03 and -0.37 standard errors
  for `psi_c` = 1.0, 1.5, 2.0. Feller-ratio sweep (`kappa 0.5, theta 0.04, rho -0.7, T 2`, 16 steps):
  Euler bias grows from 9.2e-4 (ratio 1.0) to 3.4e-2 (ratio 0.018); QE stays between 1.1e-4 and 6.0e-4,
  i.e. at noise level.
* **V-08** (`test_v08_variance_reduction.cpp`): Heston QE, Feller satisfied, 16 steps, 200,000
  plain paths, calls at 0.95 / 1.10 / 1.25 (spot 1.10, `T = 1`). Cost-adjusted variance ratios
  (plain variance divided by reduced variance; antithetic uses `2 x pair variance`):

  | strike | antithetic | control (`S_T`) | both | control coefficient |
  |---|---|---|---|---|
  | 0.95 (ITM) | 4.5 | 11.7 | 8.9 | 0.78 |
  | 1.10 (ATM) | 2.0 | 4.2 | 2.5 | 0.54 |
  | 1.25 (OTM) | 1.2 | 2.1 | 1.5 | 0.28 |

  Every estimator is within 3 standard errors of the semi-analytic price (12 checks, worst |z| 1.5);
  a control with a mean wrong by 1% of the forward is rejected at 40 standard errors. The linear
  control beats antithetic variates here, and combining them is worse than the control alone: both
  exploit the same monotone dependence of the payoff on the noise, so the gains overlap. The reported
  standard error matches the spread across 80 seeds (SE / empirical sd in the test log; an
  exploratory 400-seed run at the money gave 0.99 for plain and 1.01 for the control). Under exact GBM
  with a Garman-Kohlhagen price (strike 1.05) as the control for the strike-1.10 call the ratio is 79
  (`c` = 0.90), z = -1.4.
* **Hand mutation check, week 5** (not automated): 15 deliberate changes, all caught in the end; 2 of
  them only by the independent path table, and 1 (an inexact `1 - U` in the far tail) only after a unit
  test was added because the first run found it uncaught. Table in `docs/test_catalogue.md`.

### 5.9 Limitations

* **Euler** is badly biased when Feller fails (V-05, V-06); keep it as a baseline.
* **QE** needs `xi > 0`, has no exact-martingale guarantee in the fallback corner (steps of years),
  and its accuracy was measured only on the parameter sets above, at the money and at two other
  strikes. It is not known to be unbiased in general. Deep out-of-the-money prices, where the
  standard error is small compared with the price, were not studied.
* The only control for Heston is the terminal spot. A control with higher correlation to the payoff
  (for example the Garman-Kohlhagen payoff with the model's implied volatility) would have to have
  its mean computed under Heston, which needs the semi-analytic pricer.
* The engine passes the terminal spot to the payoff. Path-dependent payoffs (barriers, week 9) need a
  richer interface, and the `PathModel` concept will be extended then.
* Throughput and scaling numbers come from `bench/` and are specific to the machine they were
  measured on (section 5.10).

### 5.10 Benchmark (`bench/bench_mc_scaling.cpp`)

One run, in a cloud sandbox with **2 hardware threads**, GCC 13.3 Release, Heston QE (`v0 = theta =
0.04`, `kappa = 2`, `xi = 0.3`, `rho = -0.5`, `T = 1`), 16 steps, 1,000,000 paths, median of 5 runs.
Re-run it on your own machine before quoting any number; nothing here is asserted by a test.

| threads | seconds | paths/s | speed-up | efficiency | bitwise equal to 1 thread |
|---|---|---|---|---|---|
| 1 | 1.651 | 605,599 | 1.00 | 100% | (reference) |
| 2 | 0.842 | 1,187,168 | 1.96 | 98% | yes |
| 4 | 0.839 | 1,191,278 | 1.97 | 49% | yes (more threads than cores) |
| 8 | 0.783 | 1,277,036 | 2.11 | 26% | yes (more threads than cores) |

Scaling up to the number of cores is close to linear (1.96x on 2 cores); the 4 and 8 thread rows
only show that oversubscription neither helps nor changes the result. **Scaling beyond 2 cores was
not measured.** One path costs 94 ns per step with QE and 65 ns per step with Euler (1 thread), so
QE is 1.46 times as expensive per step; V-06 shows it needs far fewer steps for the same bias.

Work-normalised variance (`SE^2 x time`, one thread, equal number of path simulations, at the
money; the pilot run for the control coefficient, 20,000 paths, is not timed):

| estimator | seconds | standard error | `SE^2 x s` | gain over plain |
|---|---|---|---|---|
| plain | 1.578 | 1.36e-4 | 2.90e-8 | 1.00x |
| antithetic | 1.588 | 9.58e-5 | 1.46e-8 | 1.99x |
| control variate | 1.545 | 6.63e-5 | 6.80e-9 | 4.26x |
| antithetic + control | 1.471 | 8.49e-5 | 1.06e-8 | 2.73x |

The gains agree with the variance ratios of V-08 at the money (2.0, 4.2, 2.5), which shows the
estimators cost no more than the plain one per path simulation.

## 6. FX smile from delta quotes (week 7)

The code is in `include/fxval/smile/` and `include/fxval/numerics/normal.hpp`; the decisions are in
`docs/adr/0008-smile-construction.md`. All quotes used in tests and figures are **synthetic and
illustrative** (`configs/scenarios/smile_scenarios.csv`); no market data is used.

### 6.1 Quotes

A smile at one expiry is quoted by three numbers, for wing options of absolute delta `Delta`
(0.25 or 0.10 in practice): the at-the-money volatility `sigma_ATM`, the risk reversal
`RR = sigma_C - sigma_P` and the butterfly `BF = (sigma_C + sigma_P)/2 - sigma_ATM`. Hence

`sigma_C = sigma_ATM + BF + RR/2`,  `sigma_P = sigma_ATM + BF - RR/2`.

This is the "simple" butterfly. Markets often quote a **market strangle** butterfly, defined through
vega-weighted prices, which needs a root find; it is not implemented (limitation L-09). A wing
volatility that is not positive is rejected with a message that names the wing.

### 6.2 Delta convention (the one implemented)

* **Delta is premium-excluded**: the plain Garman-Kohlhagen delta (as if the premium were paid in the
  domestic currency).
* Two flavours of the underlying: **spot delta** `e^{-r_f T} N(d1)` for a call and
  `-e^{-r_f T} N(-d1)` for a put, and **forward delta** `N(d1)` and `-N(-d1)`. The caller chooses.
* **ATM is the delta-neutral straddle**: the strike where call delta plus put delta is zero. Since
  `N(d1) - N(-d1) = 0` means `d1 = 0`, `K_ATM = F exp(sigma_ATM^2 T / 2)` in both flavours.
* `F = S exp((r_d - r_f) T)`; rates are deterministic and flat to the expiry.

**Strike from delta.** Let `p = Delta e^{r_f T}` for spot delta and `p = Delta` for forward delta; it
must lie in (0, 1). A call with `N(d1) = p` has `d1 = N^{-1}(p)`; a put with `N(-d1) = p` has
`d1 = -N^{-1}(p)`. Since `ln(F/K) = sigma sqrt(T) d1 - sigma^2 T/2`,

`K = F exp(-sigma sqrt(T) d1 + sigma^2 T / 2)`  (with `sigma` the volatility of that wing).

The inverse normal is Acklam's rational approximation followed by one Halley step on the exact
distribution function (`numerics::normal_quantile`).

**Not implemented** (limitation L-09): premium-adjusted delta (used where the premium is paid in the
foreign currency, and for many pairs at longer tenors), the ATM-forward definition, and the switch from
spot to forward delta at long tenors that some markets use. A pair whose market uses them is not
reproduced.

### 6.3 One smile: the quadratic in log-moneyness

With `k = ln(K/F)`, the three pivots `(k_P, sigma_P)`, `(k_A, sigma_ATM)`, `(k_C, sigma_C)` define a
unique quadratic

`sigma(k) = sigma_P + b (k - k_P) + c (k - k_P)(k - k_A)`,

`b = (sigma_ATM - sigma_P)/(k_A - k_P)`, `c = [(sigma_C - sigma_ATM)/(k_C - k_A) - b]/(k_C - k_P)`.

It passes through the three market points exactly (checked by V-09) and its derivatives are analytic.
It is not a model of the smile: it is the simplest smooth interpolation, and it has two known
weaknesses that the checks report (6.5): it can become non-positive, and because `sigma` grows like
`k^2`, total variance grows like `k^4`, which **always** violates the butterfly condition far enough out
(for a calm smile with ATM 8%, RR -0.75 and BF 0.28 vol points it happens between 15 and 20 standard deviations from the forward, section 6.7). Pivot strikes must be
increasing; if the quotes, deltas and rates make them not so, construction fails (`numerical_failure`).

### 6.4 Several expiries

`VolSurface` holds smiles at strictly increasing expiries and interpolates **linearly in total
variance `w = sigma^2 T`, at fixed forward log-moneyness `k`**. That is also the coordinate in which the
calendar condition is checked, so interpolation cannot create a calendar violation that the slices do
not already have. There is **no extrapolation**: a query outside the first and last expiry is an error.

### 6.5 Arbitrage conditions

* **Butterfly (inside one expiry).** The implied density of the terminal rate is non-negative exactly
  when call prices are convex in strike. With `w(k) = sigma(k)^2 T` (Gatheral 2004),

  `g(k) = (1 - k w'/(2w))^2 - (w'^2/4)(1/w + 1/4) + w''/2`,

  and `g >= 0` is the condition. The density in strike is `g(k) exp(-d2^2/2) / (K sqrt(2 pi w))` with
  `d2 = -k/sqrt(w) - sqrt(w)/2`. For a flat smile `g = 1`.
* **Calendar (across expiries).** Total variance must not fall with maturity at fixed `k`.
* A point where the quadratic is not positive has no density and is counted separately.

The checks are evaluated on a grid of standardised log-moneyness `x = k/(sigma_ATM sqrt(T))` (default
`x` from -4 to 4, 801 points; the calendar check uses the grid of the earlier expiry). A violation is
a value below `-1e-12` (rounding level). Each report gives the counts, the smallest `g`, and the
**innermost** violation (closest to the forward) in standard deviations, strike and absolute delta:
that number says how far into the wings the smile can be used. Nothing is repaired.

### 6.6 Evidence

* **V-09** (20,160 cases): quote to strike to delta back to quote, worst error 2.0e-14 against a
  criterion of 1e-8; a wrong-convention control misses by at least 1.2e-3.
* **SMILE-REF** (256 rows): independent Python implementation (root finding for strikes, Vandermonde fit),
  worst strike difference 1.1e-15 relative.
* **V-10**: the density factor equals the second difference of option prices on 3,165 grid points
  (67 negative, so the sign is tested); flat smile clean; designed calendar inversion and concave smile
  flagged exactly as predicted from theory before the run.
* 25 hand mutations, 24 caught (test catalogue).

### 6.7 Observed results on the synthetic scenarios (default grid, `x` in [-4, 4])

| Scenario | Butterfly | Calendar |
|---|---|---|
| flat | clean | clean |
| calm_g10 (EURUSD-like, 1W to 2Y) | clean, smallest `g` 0.18 | clean |
| stressed (ATM 14% falling to 11.5%, RR -3 to -1.8) | clean, smallest `g` 0.28 | violations 3M to 6M (34 points) and 1Y to 2Y (61 points), at 3.55 to 3.72 standard deviations, 10 to 11 delta |
| emerging (USD/PLN-like, RR +1.2 to +2.2) | clean, smallest `g` 0.33 | violations 3M to 6M (41 points, 3.8 standard deviations, 4.9 delta) and 6M to 1Y (86 points, 3.5 standard deviations, 5.1 delta) |
| extreme_skew (RR -8, ATM 10%) | violations: 83, 87, 100 points at 1M, 3M, 1Y; innermost at 0.12 to 0.27 standard deviations and about 40 delta; the polynomial is not positive at 376 to 411 points | not defined where a slice is not positive |
| concave (BF -4) | violations: 69 points, innermost at the forward; not positive at 677 points | n/a (one expiry) |
| calendar_inversion | clean | 1W to 2W violates at all 801 points; 2W to 1M clean |

The calm scenarios are clean on the default grid. A separate probe of a calm 1-year smile (ATM 8%,
RR -0.75, BF 0.28 vol points; the unit-test smile) found the density factor positive at 15 standard
deviations and negative at 20, so the quadratic fails only far outside any traded strike range. The calendar violations of the stressed and emerging scenarios are the more useful finding: the
quotes at each expiry are individually reasonable, yet interpolated total variance falls from one
expiry to the next in the wings, (a likely cause, not tested separately: the risk reversal and butterfly
term structure changes faster than the ATM term structure grows). Options priced from such a surface in the 5 to 11 delta range could
be priced inconsistently across maturities (finding F-09 in the validation report).

Figures: `docs/results/smile_<scenario>.png`, produced by
`fxval-example-smile` and `python/plot_smiles.py`.

### 6.8 Limitations of the smile construction

L-09 market conventions (premium-adjusted delta, market-strangle butterfly, ATM forward) are not
implemented; L-10 the quadratic is not arbitrage-free in the far wings by construction; L-11 only the
three pivots are fitted, so a ten-delta quote cannot be used together with a 25-delta one; L-12
interpolation in time at fixed forward log-moneyness is not the same as at fixed delta or at fixed
strike, and the surface takes its query in `k`, so the caller supplies the forward.

## 7. to 10. Planned sections

* **7. Vanna-Volga** (week 9): which market variant of the barrier weighting is used, and how
  much the choice changes the price.
* **8. Barrier options** (week 9): closed form (Reiner-Rubinstein) as benchmark, Monte Carlo with
  Brownian-bridge correction for discrete monitoring, model comparison (V-12 to V-15).
* **9. Market risk** (week 10): EWMA and GARCH volatility, historical, parametric, delta-gamma,
  full revaluation and filtered historical simulation, backtests (V-16 to V-18, V-21).
* **10. Counterparty credit risk** (week 11): exposure simulation, netting, EE, PFE, CVA with an
  assumed flat hazard rate, checks against closed forms (V-19, V-20).

## 11. Limitations (running list)

* FX option quotes are paid data. Smile inputs will be synthetic and labelled as such.
* Interest rates are deterministic in v1.0. Long-dated exposure is therefore understated
  relative to a model with stochastic rates (planned for v1.1).
* Default probabilities in the CVA calculation are an assumption, not market-implied.
* Independent validation of Garman-Kohlhagen so far is put-call parity and finite-difference
  Greeks (V-01, V-02), not a market-price comparison.
* Smile quotes are synthetic. The smile code implements one delta convention, the simple butterfly
  and a quadratic interpolation (section 6.8). The Heston calibration uses three quotes per expiry and is tested on
  quotes made by Heston itself (section 4.11).

## 12. References

Verify the bibliographic details of each entry before citing it in the validation report.

* M. Garman and S. Kohlhagen (1983), *Foreign currency option values*, Journal of International
  Money and Finance 2(3).
* S. Heston (1993), *A closed-form solution for options with stochastic volatility*, Review of
  Financial Studies 6(2).
* J. Gatheral (2004), *A parsimonious arbitrage-free implied volatility parameterization with
  application to the valuation of volatility derivatives*, Global Derivatives & Risk Management.
* P. Acklam, an algorithm for computing the inverse normal cumulative distribution function (web note).
* I. Clark (2011), *Foreign Exchange Option Pricing*, Wiley; U. Wystup (2006), *FX Options and
  Structured Products*, Wiley.
* H. Albrecher, P. Mayer, W. Schoutens and J. Tistaert (2007), *The little Heston trap*, Wilmott
  Magazine.
* K. Levenberg (1944), *A method for the solution of certain non-linear problems in least squares*, Quarterly of
  Applied Mathematics 2; D. Marquardt (1963), *An algorithm for least-squares estimation of nonlinear parameters*,
  SIAM Journal 11; H. Nielsen (1999), *Damping parameter in Marquardt's method*, IMM technical report; R. Brent (1973),
  *Algorithms for Minimization without Derivatives*, Prentice-Hall.
* R. Lord, R. Koekkoek and D. van Dijk (2010), *A comparison of biased simulation schemes for
  stochastic volatility models*, Quantitative Finance 10(2).
* J. Salmon, M. Moraes, R. Dror and D. Shaw (2011), *Parallel random numbers: as easy as 1, 2, 3*,
  Proceedings of SC11.
* T. Chan, G. Golub and R. LeVeque (1983), *Algorithms for computing the sample variance*, The
  American Statistician 37(3).
* L. Andersen (2008), *Simple and efficient simulation of the Heston stochastic volatility
  model*, Journal of Computational Finance 11(3).
* M. Reiner and M. Rubinstein (1991), *Breaking down the barriers*, Risk 4(8).
* A. Castagna and F. Mercurio (2007), *The vanna-volga method for implied volatilities*, Risk.
* I. Clark (2011), *Foreign Exchange Option Pricing*, Wiley.
* U. Wystup (2017), *FX Options and Structured Products*, 2nd edition, Wiley.
* P. Kupiec (1995), *Techniques for verifying the accuracy of risk measurement models*, Journal
  of Derivatives 3(2).
* P. Christoffersen (1998), *Evaluating interval forecasts*, International Economic Review 39(4).
