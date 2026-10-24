# Portfolio application: mathematical documentation

This document gives the mathematics of `examples/portfolio` (the program `fxval-portfolio`): what is computed for an FX book,
which formula is behind each number in the report, and which function and test carry it. It builds on the model chapters of
`docs/model_documentation.md` and does not repeat their derivations; it shows how the pieces are combined and what each
combination assumes. The program is an example: its data is invented (see section 11), and the numbers quoted below come from
the default scenario.

Formulas use LaTeX (GitHub renders it). Symbols are the library's: $S$ spot, $r_d$ domestic and $r_f$ foreign continuously
compounded rate, $K$ strike, $T$ time to expiry in years, $\sigma$ volatility, $F=S e^{(r_d-r_f)T}$ the forward.
$N(\cdot)$ and $\varphi(\cdot)$ are the standard normal distribution and density.

## 1. Conventions

| Item | Convention |
|---|---|
| Pair | one pair, EURUSD: the *foreign* currency is EUR, the *domestic* currency is USD; $S$ is USD per EUR |
| Units | a position of $q$ is $q$ units of EUR; every value, Greek, VaR and CVA is in USD, as of today |
| Sign | long is positive $q$. P&L is positive for a gain. VaR, ES and CVA are reported as **positive losses** |
| Rates | flat, deterministic, continuously compounded, one pair of rates for the whole book |
| Time | year fractions; theta is per year in the data and divided by 365 for display |

## 2. The book

The book is a set of forwards and European vanilla options on the same pair:

$$
V(S) \;=\; \sum_{i\in\text{fwd}} V^{\text{fwd}}_i(S) \;+\; \sum_{j\in\text{opt}} q_j\, P_j(S),
$$

with every volatility, rate and date held fixed when $S$ moves (section 7 says why this matters).

### 2.1 Forward

A forward on $N$ units of EUR with strike $K$ and maturity $T$ pays $N(S_T-K)$ in USD:

$$
V^{\text{fwd}}(S) \;=\; N\left(S e^{-r_f T} - K e^{-r_d T}\right).
$$

It is linear in $S$. It is also the right-hand side of put-call parity (section 3.4 of `model_documentation.md`), which the
program uses as a test: a long call and a short put with the same strike and expiry are worth exactly a forward.

### 2.2 Vanilla option (Garman-Kohlhagen)

$$
P_{\text{call}} = S e^{-r_f T}N(d_1) - K e^{-r_d T}N(d_2),\qquad
P_{\text{put}} = K e^{-r_d T}N(-d_2) - S e^{-r_f T}N(-d_1),
$$

$$
d_1=\frac{\ln(S/K)+(r_d-r_f+\tfrac12\sigma^2)T}{\sigma\sqrt T},\qquad d_2=d_1-\sigma\sqrt T .
$$

Code: `pricing::garman_kohlhagen_price`. Details and validation: `model_documentation.md` section 3.

### 2.3 The volatility of each option: the smile

The volatility is not one number for the book. Each option takes the volatility of the surface at its own strike and expiry.

1. The market quotes, per expiry, an ATM volatility, a 25-delta risk reversal $RR$ and a 25-delta butterfly $BF$.
   The wing volatilities are $\sigma_{C}=\sigma_{ATM}+BF+\tfrac12 RR$ and $\sigma_{P}=\sigma_{ATM}+BF-\tfrac12 RR$
   (`smile::pivot_volatilities`; section 6.1 of `model_documentation.md`).
2. The delta convention turns the three volatilities into three strikes $K_P<K_{ATM}<K_C$ (section 6.2 there).
3. With $k=\ln(K/F)$, a quadratic through the three pivots gives $\sigma(k)$ for one expiry (section 6.3).
4. Between two quoted expiries the surface interpolates **total variance** $w=\sigma^2 T$ linearly in $T$ at the same $k$
   (`smile::VolSurface::total_variance`), and $\sigma(k,T)=\sqrt{w(k,T)/T}$. There is **no extrapolation**: an option with an
   expiry outside the quoted range (1M to 1Y in the default scenario) is refused with an error that names the option.

For an option the program evaluates $\sigma_j=\sigma(\ln(K_j/F_j),T_j)$ **once, at today's spot**, and keeps it fixed afterwards
(sticky strike; section 7.2). Test: an option struck at the 25-delta call strike of the 3M slice gets exactly
$\sigma_{ATM}+BF+\tfrac12RR$ (`AnOptionAtAPivotStrikeGetsThePivotVolatilityOfItsSlice`).

## 3. Greeks

### 3.1 Option Greeks

For one unit, the nine Greeks of `pricing::garman_kohlhagen_greeks` are those of section 3.5 of `model_documentation.md`
(delta $e^{-r_fT}N(d_1)$ for a call, gamma $e^{-r_fT}\varphi(d_1)/(S\sigma\sqrt T)$, vega $Se^{-r_fT}\varphi(d_1)\sqrt T$, and so on).
A position with quantity $q$ has $q$ times each of them.

### 3.2 Forward Greeks

The library has no Greeks for a forward, so the program derives them from $V^{\text{fwd}}$ (code: `forward_greeks`). With $\tau=T$:

$$
\Delta = N e^{-r_fT},\qquad \Gamma=0,\qquad \text{vega}=\text{vanna}=\text{volga}=0,
$$

$$
\Theta=\frac{\partial V}{\partial t}=-\frac{\partial V}{\partial T}=N\left(r_f S e^{-r_fT}-r_d K e^{-r_dT}\right),
$$

$$
\rho_d=\frac{\partial V}{\partial r_d}=N\,T K e^{-r_dT},\qquad
\rho_f=\frac{\partial V}{\partial r_f}=-N\,T S e^{-r_fT}.
$$

Test: each of $\Theta,\rho_d,\rho_f$ equals a central finite difference of the revalued forward
(`ForwardGreeksMatchFiniteDifferences`, step $10^{-6}$).

### 3.3 Aggregation and display

Greeks of the book are sums over positions, because the book value is a sum: $\partial V/\partial x=\sum_i\partial V_i/\partial x$.
The report shows scaled versions of the raw derivatives:

| Shown | Formula | Meaning |
|---|---|---|
| Delta (units) | $\Delta$ | EUR of spot exposure |
| Delta per 1% move | $0.01\,S\,\Delta$ | USD gained for a 1% rise of the spot, first order |
| Gamma, 1% move | $\tfrac12\,\Gamma\,(0.01\,S)^2$ | second-order USD change for a 1% move |
| Vega per vol point | $\text{vega}/100$ | USD for one volatility point (0.01) |
| Theta per day | $\Theta/365$ | USD per calendar day |
| Rho per bp | $\rho/10^4$ | USD for one basis point of the rate |

Test: $\Delta$ and $\Gamma$ of the total equal central finite differences of the whole-book revaluation
(`DeltaAndGammaMatchFiniteDifferencesOfTheRevaluation`).

## 4. Two cross-checks of the option values

The report prints, next to the smile value, two other prices of the same option. They measure how much the answer depends on
the model; they are not a validation of any one of them.

* **Flat ATM volatility.** The same Garman-Kohlhagen formula with $\sigma_{ATM}(T)$ instead of $\sigma(k,T)$. The difference is
  the smile premium of that strike (large for the 1Y put in the default book: $92{,}874$ with the smile against $80{,}680$ flat).
* **Heston.** $dS=(r_d-r_f)S\,dt+\sqrt v\,S\,dW^S$, $dv=\kappa(\theta-v)dt+\xi\sqrt v\,dW^v$, $d\langle W^S,W^v\rangle=\rho\,dt$
  (section 4 of `model_documentation.md`). The five parameters $(v_0,\kappa,\theta,\xi,\rho)$ are calibrated by least squares to
  the pivot volatilities of all four expiries (`calibration::calibrate_heston`), then each option is priced with the
  semi-analytic Lewis formula (`models::heston_price`). The report shows the calibration's root-mean-square volatility
  error; the default scenario fits to $0.06$ volatility points. A Heston price close to the smile price says the smile model
  is consistent with a dynamic model at these strikes; a large gap would flag model risk.

## 5. Spot ladder

For a relative spot move $x$ the program revalues the whole book: $S_x=S(1+x)$,

$$
\text{P\&L}(x)=V(S_x)-V(S),
$$

and compares it with the second-order expansion

$$
\text{P\&L}_{\text{Taylor}}(x)=\Delta\,(S_x-S)+\tfrac12\,\Gamma\,(S_x-S)^2 .
$$

The difference is $\tfrac16 V'''(S)(S_x-S)^3+\dots$ (third order), which is the part of the risk a delta-gamma method does not see.
Test: both are zero at $x=0$, the error at $x=1\%$ is more than 100 times smaller than at $10\%$
(`LadderIsFullRevaluationAndTheTaylorExpansionIsLocallyAccurate`).

## 6. Market risk

### 6.1 What the risk layer is given

`risk::FxPortfolio` holds a spot position of $u$ units and a list of vanilla legs. The book has forwards, so they are mapped.
A forward's P&L under a spot move is

$$
V^{\text{fwd}}(S')-V^{\text{fwd}}(S)=N e^{-r_fT}\,(S'-S),
$$

which is the P&L of $N e^{-r_fT}$ units of spot (the constant $-NKe^{-r_dT}$ cancels). So

$$
u=\sum_{i\in\text{fwd}}N_i e^{-r_fT_i},
$$

and every option becomes a leg with its smile volatility (code: `risk_portfolio`). The mapping is exact for P&L, not for value:
the risk layer's portfolio value differs from the book value by the constant, which VaR never uses. Test: the ladder's P&L
equals `risk::pnl_for_simple_return` of a portfolio rebuilt by hand in the test, at every ladder point
(`RiskLayerSeesTheSamePnlAsTheLadder`).

The only risk factor is the spot. Volatility, rates and dates are held fixed, so there is **no vega, theta or rate risk** in
the VaR numbers (L-26 in the validation report). The one-day horizon uses one-day returns directly; no square-root-of-time scaling.

### 6.2 Volatility forecast: GARCH(1,1)

Daily log returns $r_t$ follow

$$
r_t=\sqrt{v_t}\,z_t,\qquad v_{t+1}=\omega+\alpha\,r_t^2+\beta\,v_t,\qquad z_t\sim\text{iid}(0,1),
$$

fitted by Gaussian maximum likelihood,

$$
\ell(\omega,\alpha,\beta)=-\tfrac12\sum_{t=1}^{n}\left[\ln(2\pi)+\ln v_t+\frac{r_t^2}{v_t}\right],
$$

with the Nelder-Mead optimiser of `numerics/nelder_mead.hpp` (section 9.2-9.3 of `model_documentation.md`). The next-day volatility is
$\sigma_{n+1}=\sqrt{v_{n+1}}$ from the last element of the variance path, and the unconditional volatility is
$\sqrt{\omega/(1-\alpha-\beta)}$. In the default scenario $\sigma_{n+1}=0.491\%$ and the persistence $\alpha+\beta$ is $0.958$.

The report says "not converged" when the optimiser's simplex tolerance is not met. The likelihood in $(\alpha,\beta)$ is flat along the
persistence ridge, so this happens for some samples even when the estimates are sensible (for example, 750 days with the default
seed gives $\alpha=0.053,\ \beta=0.895$ and the flag). That is why the default history is 1500 days. Test: the forecast, $\alpha$, $\beta$ and the
unconditional volatility equal those of an independent call of `fit_garch` on the same returns
(`TheVolatilityForecastIsTheNextDayVarianceOfAnIndependentGarchFit`).

### 6.3 The five methods

Let $\alpha_c$ be the confidence level (0.99), $q=1-\alpha_c$, $z_q$ the $q$-quantile of $N(0,1)$ (negative), and
$\text{P\&L}=\Delta S x+\tfrac12\Gamma S^2x^2+\dots$ with $x$ the one-day return of the spot. VaR and ES are losses.

1. **Delta-normal.** $x\sim N(0,\sigma_{n+1}^2)$, linear P&L:

   $$\text{VaR}=-z_q\,\sigma_{n+1}\,|\Delta|\,S,\qquad \text{ES}=\sigma_{n+1}|\Delta|S\,\varphi(z_q)/q .$$

   Example (default scenario): $2.3263\times0.0049134\times9{,}904{,}024\times1.085=122{,}828$, which is the number in the report.
   Test: for a forward-only book the program's number equals this closed form to $10^{-9}$ relative
   (`VarOfALinearBookIsTheNormalQuantileTimesItsVolatility`).
2. **Delta-gamma.** $\text{P\&L}=c_1w+c_2w^2$, $w\sim N(0,1)$, $c_1=\Delta S\sigma$, $c_2=\tfrac12\Gamma S^2\sigma^2$. The exact moments
   of this quadratic form are the mean $c_2$, the variance $c_1^2+2c_2^2$, the third central moment $6c_1^2c_2+8c_2^3$ and the
   fourth $3c_1^4+60c_1^2c_2^2+60c_2^4$ (section 9.5 there), and the VaR is the Cornish-Fisher quantile
   $z_{cf}=z+\tfrac{z^2-1}{6}s+\tfrac{z^3-3z}{24}\kappa-\tfrac{2z^3-5z}{36}s^2$ with skewness $s$ and excess kurtosis $\kappa$ of
   those moments. Its ES is that of the quantile grid of the same quadratic (no closed form exists).
3. **Full revaluation, normal returns.** The whole book is revalued at the $N=100{,}000$ quantile points $x_i=\sigma_{n+1}z_{(i-1/2)/N}$ and the historical
   VaR of those P&Ls is taken. It captures all the curvature of the book but assumes normal returns.
4. **Historical simulation.** The 1500 observed log returns $r_t$ are applied as $S e^{r_t}$ and the book is revalued;
   VaR is the $k$-th worst P&L, $k=\lceil nq\rceil$, ES the mean of the $k$ worst. This keeps the observed tails and the observed
   volatility of the whole sample, not today's.
5. **Filtered historical simulation.** The standardised residuals $\hat z_t=r_t/\sqrt{v_t}$ keep the shape of the tails; they are scaled
   to today's volatility, $r=\sigma_{n+1}\hat z_t$, and the book is revalued (Barone-Adesi, Giannopoulos and Vosper 1999).

The histogram in the report is the distribution of the P&L of method 4, with its VaR marked.

Reading the table: when today's volatility ($0.49\%$) is below the sample average ($0.55\%$), the methods that use today's
volatility (1, 2, 3, 5) give smaller numbers than the plain historical one (4). The size of the gap between them is information
about volatility, not about the portfolio.

## 7. Assumptions behind the ladder and the VaR

* **A-1, spot only.** Rates, volatilities and dates do not move. A real VaR of an option book has vega risk that is often larger
  than its spot risk. (L-26.)
* **A-2, sticky strike.** Each option keeps the volatility it has today when the spot moves. Under sticky delta the volatility
  would move with $k=\ln(K/F)$ and the ladder would have a different gamma and extra vanna terms. The program does not model that.
* **A-3, one pair, one currency pair of rates.** No cross-currency effects and no basis.
* **A-4, expiries inside the surface.** The surface is not extrapolated in expiry; an option outside the quoted range is refused.
* **A-5, European vanillas and forwards.** No barriers, no early exercise. The library prices barriers
  (`pricing/barrier_closed_form.hpp`) but the risk layer and the exposure simulator take vanillas and forwards only.

## 8. Counterparty credit risk

### 8.1 Exposure

The netting set is the whole book, one counterparty, no collateral. The spot is simulated exactly as a geometric Brownian motion on the grid
$0=t_0<t_1<\dots<t_n$, risk-neutral drift $r_d-r_f$:

$$
S_{t_j}=S_{t_{j-1}}\exp\!\left[\left(r_d-r_f-\tfrac12\sigma_e^2\right)\Delta t_j+\sigma_e\sqrt{\Delta t_j}\,Z_j\right].
$$

The simulated volatility $\sigma_e$ is one number: the smile ATM volatility at half the longest maturity, kept inside the quoted expiries (7.71% by default). Each
option is revalued along the path with its own smile volatility held constant (section 7.1 of `model_documentation.md`, L-30);
that is inconsistent with a model in which the volatility moves, and it is chosen to be simple. Value of the netting set on a path
at date $t$:

$$
V_t=\sum_i V^{\text{fwd}}_i(S_t,T_i-t)+\sum_j q_jP_j(S_t,T_j-t),
$$

where a trade past its maturity is worth $0$. Exposure is $\max(V_t,0)$. For each date

$$
\text{EE}(t)=\mathbb E[\max(V_t,0)],\qquad
\text{PFE}_{\beta}(t)=\text{the }\beta\text{-quantile of }\max(V_t,0)\ (\beta=95\%),
$$

$$
\text{EPE}=\frac{1}{t_n}\int_0^{t_n}\text{EE}(t)\,dt\quad(\text{trapezoid}).
$$

EE and PFE step down at each maturity because the matured trades leave the netting set. The expected exposure of a short option
is zero (its value is never positive).

### 8.2 CVA

With a flat hazard rate $\lambda$ (survival $Q(t)=e^{-\lambda t}$), recovery $R$, deterministic discounting $DF(t)=e^{-r_dt}$ and
independence between default and exposure,

$$
\text{CVA}=(1-R)\int_0^{T}DF(t)\,\text{EE}(t)\,dQ^{\ast}(t),\qquad Q^{\ast}(t)=1-Q(t)\ (\text{default probability}),
$$

discretised by the trapezoid rule on the product $DF\cdot\text{EE}$:

$$
\text{CVA}=(1-R)\sum_{i=1}^{n}\frac{DF(t_{i-1})\text{EE}(t_{i-1})+DF(t_i)\text{EE}(t_i)}{2}\,\bigl(Q(t_{i-1})-Q(t_i)\bigr).
$$

The code applies the same weights path by path to $\max(V,0)$, which gives the standard error across paths. With the default
$\lambda=3\%$ and $R=40\%$ (a credit spread of roughly $\lambda(1-R)=180$ bp) the netted CVA is about 1,357 with a standard error of about 8.
Tests: the reported CVA equals this formula evaluated in the test on the reported exposure profile
(`CvaEqualsTheTrapezoidFormulaOnTheReportedExposureProfile`); CVA is linear in $(1-R)$ and zero for $\lambda=0$
(`CvaScalesWithLossGivenDefaultAndVanishesWithoutHazard`); a book of short options has CVA exactly 0.

### 8.3 Netting benefit

The standalone CVA of trade $i$ is the CVA of a netting set holding only that trade, on the same simulated paths. Because
$\max\bigl(\sum_iV_i,0\bigr)\le\sum_i\max(V_i,0)$ on every path and every weight is non-negative,

$$
\text{CVA}_{\text{netted}}\;\le\;\sum_i\text{CVA}_i,\qquad \text{netting benefit}=\sum_i\text{CVA}_i-\text{CVA}_{\text{netted}}\ \ge 0 .
$$

In the default scenario the sum of the standalone CVAs is $3{,}255$ against $1{,}357$ netted: the short straddle and the short put
offset the long exposures. A single-trade set has no benefit (`ASingleTradeHasNoNettingBenefit`).
Limits: no collateral or margin period of risk (L-28), no wrong-way risk (L-29), the hazard rate is one flat number, no funding or capital adjustments.

## 9. Map from report quantity to formula, code and test

| Report quantity | Section | Code | Test |
|---|---|---|---|
| Option volatility | 2.3 | `resolve` (`VolSurface::vol`) | `AnOptionAtAPivotStrikeGetsThePivotVolatilityOfItsSlice` |
| Position value, total | 2 | `book_value`, `garman_kohlhagen_price` | `ShortAndLongOptionsAtTheSameStrikeGiveTheForward`, `TotalsAreTheSumOfThePositions` |
| Forward Greeks | 3.2 | `forward_greeks` | `ForwardGreeksMatchFiniteDifferences` |
| Total delta, gamma | 3.3 | `analyse` | `DeltaAndGammaMatchFiniteDifferencesOfTheRevaluation` |
| Flat ATM and Heston values | 4 | `calibrate_heston`, `heston_price` | `DefaultScenarioRunsWithHestonAndEverythingIsFinite` |
| Ladder, Taylor | 5 | `analyse` (ladder) | `LadderIsFullRevaluationAndTheTaylorExpansionIsLocallyAccurate` |
| Book to risk layer | 6.1 | `risk_portfolio` | `RiskLayerSeesTheSamePnlAsTheLadder` |
| GARCH forecast | 6.2 | `fit_garch` | `TheVolatilityForecastIsTheNextDayVarianceOfAnIndependentGarchFit` |
| VaR, ES (five methods) | 6.3 | `risk::*_var_es` | `VarOfALinearBookIsTheNormalQuantileTimesItsVolatility` |
| P&L histogram | 6.3 | `histogram` | `HistogramCountsEveryScenario` |
| Exposure, EPE | 8.1 | `simulate_netting_set_values`, `exposure_profile` | `NettedCvaIsAtMostTheSumOfTheStandaloneCvas` |
| CVA | 8.2 | `cva_estimate`, `cva_from_profile` | `CvaEqualsTheTrapezoidFormulaOnTheReportedExposureProfile` |
| Netting benefit | 8.3 | `analyse` (credit) | `ASingleTradeHasNoNettingBenefit` |

The tests are in `tests/example/test_portfolio_analysis.cpp`. They were run against deliberate mistakes in the application code
(a flipped sign of a forward Greek, a missing discount factor in the forward-to-spot mapping, the wrong rate in the CVA, the
GARCH forecast taken one step early, a Taylor expansion without gamma, a wrong smile lookup, a wrong sign of the netting benefit,
the exposure volatility read outside the quoted expiries, the maximum of a histogram put in the wrong bin):
each one makes at least one test fail.

## 10. Reproducing the numbers

```sh
cmake --preset release
cmake --build --preset release --target fxval-portfolio
./build/release/examples/portfolio/fxval-portfolio --html report.html --json report.json
```

Options: `--returns file.csv` (daily log returns, one per line or `date,return`), `--level`, `--paths`, `--seed`, `--no-heston`.
Everything is deterministic for a given seed and, because every Monte Carlo path has its own random stream, independent of the number of
threads. Several of the figures above were also recomputed in Python with only the standard library: the forward value $18{,}960.73$,
its delta $4{,}896{,}094.8$ and theta $-89{,}455.2$ per year, the 3M 1.10 call at $7.4075\%$ volatility with value $113{,}845.61$ and
delta $4{,}033{,}237.9$, and the delta-normal VaR $122{,}828.40$. They agree with the program to the printed digits.

## 11. Limits of the example

The default scenario is synthetic: the surface, the rates, the trades and the return history are invented to exercise the
code. The GARCH history comes from the library's random number generator, so its fit recovers the process it was drawn from and says
nothing about the real EURUSD. For a real use the market data, the returns (`--returns`) and the credit inputs (hazard rate from a CDS
curve, recovery from convention) have to be supplied, and the assumptions A-1 to A-5 and the limitations cited above have to be accepted
or removed.
