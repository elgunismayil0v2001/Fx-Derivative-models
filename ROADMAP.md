# Roadmap

v1.0 scope is frozen. New ideas go in the "Later" list below, not into the v1.0 plan.

## v0.5 (end of week 6)

Garman-Kohlhagen with Greeks, Heston semi-analytic pricer, Monte Carlo engine with Euler full
truncation and QE schemes, control variate and antithetics, thread-count-invariant results, and a
first validation write-up on Heston and the Monte Carlo schemes (`docs/validation_report.md`), and
the `fxval-price` command-line program with validated configuration and run metadata.
Tests V-01 to V-08.

## v0.6 (end of week 7)

FX smile from ATM, risk-reversal and butterfly quotes with one stated delta convention, quadratic
interpolation, a surface interpolated in total variance, and butterfly and calendar arbitrage reports.
Tests V-09 and V-10.

## v0.7 (end of week 8)

Implied volatility, Brent and Levenberg-Marquardt solvers, the Heston model in market units, and
Heston calibration to delta quotes with parameter recovery, identification control and a noise study.
Test V-11.

## v0.8 (end of week 9)

Barrier, one-touch and double no-touch options: Reiner-Rubinstein closed forms checked against independent
numerical integration, Monte Carlo with the Brownian-bridge correction (Black-Scholes and Heston), Vanna-Volga
(three variants) and a comparison of Garman-Kohlhagen, Vanna-Volga and Heston with the spread reported in vol
points and percent of notional. Tests V-12 to V-15, BARRIER-REF.

## v0.9 (end of week 10)

Market risk: EWMA and GARCH volatility estimation checked against independent code, historical, normal,
Cornish-Fisher, delta-gamma, full-revaluation and filtered-historical VaR/ES, Kupiec, Christoffersen and Basel
traffic-light backtests, a look-ahead check, and the quantified gap between delta-normal and full
revaluation. Tests V-16, V-17, V-18, V-21, RISK-REF.

## v0.10 (end of week 11)

Counterparty credit risk: exposure simulation for FX forwards and options, netting, EE, PFE, unilateral CVA with a
flat hazard rate, checked against Black-76, an exact CVA identity and nested simulation. Tests V-19, V-20, CREDIT-REF.

## v0.11 (end of week 12)

The validation report as a single release-candidate document: executive summary, open findings, models and approach
brought up to date, mutation-testing history, and a traceability matrix checked by a test.

## v1.0

Smile construction from delta quotes, Heston calibration with parameter recovery, Vanna-Volga and
barrier model comparison, VaR/ES with backtests, exposure and CVA, command-line apps with
schema-validated configuration and run metadata, and the complete validation report.
Tests V-01 to V-21.

## v1.1 (optional, only after v1.0 is published)

* Dupire local volatility (finite-difference PDE) as a third barrier model
* Hull-White stochastic rates and an FX hybrid for long-dated exposure
* LSTM volatility forecast compared with GARCH on out-of-sample loss

## Later

* Python bindings (pybind11) with type stubs
* Wheels via cibuildwheel
* MSVC build
* Local-stochastic-volatility model for exotics
