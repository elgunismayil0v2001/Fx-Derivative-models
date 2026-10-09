# Roadmap

v1.0 scope is frozen. New ideas go in the "Later" list below, not into the v1.0 plan.

## v0.5 (end of week 6)

Garman-Kohlhagen with Greeks, Heston semi-analytic pricer, Monte Carlo engine with Euler full
truncation and QE schemes, control variate and antithetics, thread-count-invariant results, and a
first validation write-up on Heston and the Monte Carlo schemes. Tests V-01 to V-08.

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
