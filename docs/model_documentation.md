# Model documentation

Status: **draft, week 3.** Sections marked *planned* are placeholders that name the week in
which they are written. Garman-Kohlhagen is implemented and checked by V-01 and V-02. The Heston
semi-analytic pricer is implemented and checked by V-03, V-04 and an independent Python table.

| Section | Status |
|---|---|
| 1. Purpose and scope | draft |
| 2. Notation and conventions | draft |
| 3. Garman-Kohlhagen (benchmark) | implemented; V-01 and V-02 passing |
| 4. Heston | semi-analytic pricer implemented; V-03, V-04, reference table passing (week 3); calibration week 8 |
| 5. Monte Carlo schemes | planned, weeks 4-5 |
| 6. FX smile from delta quotes | planned, week 7 |
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
* Parameters are constant in time; the model is fitted per expiry (calibration, week 8).
* Interest rates are deterministic (as in the rest of v1.0).
* The `not_converged` path is not exercised by a test: no input is known that triggers it.
* Not covered yet: Heston Greeks (week 8, by finite differences of the price), implied volatility
  output, and exotic payoffs (week 9 by Monte Carlo).

## 5. Monte Carlo

Status: the engine, exact GBM and the Heston Euler scheme with full truncation (week 4). The QE
scheme, variance reduction and the Feller study follow in week 5.

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

### 5.6 Limitations

* The Euler scheme is badly biased when Feller fails: in the stress case above the bias is still
  7.8 standard errors at 128 steps. It is a baseline, not the recommended scheme; QE (week 5) is.
* No variance reduction yet (week 5). Standard errors scale as `1/sqrt(N)` only.
* The engine passes the terminal spot to the payoff. Path-dependent payoffs (barriers, week 9)
  need a richer interface, and the `PathModel` concept will be extended then.
* The throughput quoted for planning (about 3.5e7 time steps per second on two threads in the
  development sandbox) is one unrepeated measurement, not a benchmark. Scaling benchmarks: week 5.

## 6. to 10. Planned sections

* **Week 5 additions to section 5:** Andersen QE scheme, control variate using the GK price,
  antithetic variates, the Feller study (V-06, V-08).
* **6. FX smile from delta quotes** (week 7): ATM, 25-delta risk reversal and butterfly. The
  delta convention (spot or forward delta, premium included or not, ATM definition) must be
  chosen once, written here, and tested (V-09).
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

## 12. References

Verify the bibliographic details of each entry before citing it in the validation report.

* M. Garman and S. Kohlhagen (1983), *Foreign currency option values*, Journal of International
  Money and Finance 2(3).
* S. Heston (1993), *A closed-form solution for options with stochastic volatility*, Review of
  Financial Studies 6(2).
* H. Albrecher, P. Mayer, W. Schoutens and J. Tistaert (2007), *The little Heston trap*, Wilmott
  Magazine.
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
