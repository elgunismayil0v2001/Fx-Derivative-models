# Architecture

Status: release 1.0. All layers below exist: `core`, `numerics`, `pricing`, `models` (Heston), `mc`, `smile`, `calibration`, `exotics`, `timeseries`, `risk` and `credit`, the command-line program `fxval-price` in `apps`,
and the example application `fxval-portfolio` in `examples/portfolio`. The diagrams in the next section are drawn from the actual `#include` lines of the library (checked when they were written); the layer list after them is the
rule they must satisfy.

## Diagrams

### Library modules and their direct dependencies

An arrow means "includes headers of". Only the direct dependencies that are not implied by another path are drawn (for example
`mc` depends on `pricing` through `models`). Everything depends on `core`, which is not drawn for that reason.

```mermaid
flowchart TB
  classDef lib fill:#e8f0fe,stroke:#4a6fa5,color:#1b2430
  classDef top fill:#fdf1e3,stroke:#c08a3e,color:#1b2430
  core["core<br/>errors, typed units, limits, contracts, day count, SHA-256"]:::lib
  numerics["numerics<br/>quadrature, normal, Brent, Nelder-Mead, Levenberg-Marquardt"]:::lib
  pricing["pricing<br/>Garman-Kohlhagen, implied vol, barrier closed forms"]:::lib
  models["models<br/>Heston characteristic function and price"]:::lib
  mc["mc<br/>Philox streams, GBM, Heston Euler and QE, barrier MC"]:::lib
  smile["smile<br/>delta quotes, smile slice, vol surface, arbitrage checks"]:::lib
  calibration["calibration<br/>Heston smile map and calibrator"]:::lib
  exotics["exotics<br/>Vanna-Volga, GK / VV / Heston comparison"]:::lib
  timeseries["timeseries<br/>EWMA, GARCH, GJR-GARCH"]:::lib
  risk["risk<br/>VaR, ES, five portfolio methods, backtests"]:::lib
  credit["credit<br/>exposure, netting, EE, PFE, CVA"]:::lib
  price["apps/price<br/>fxval-price (JSON in, report out)"]:::top
  portfolio["examples/portfolio<br/>fxval-portfolio (value, Greeks, VaR, CVA)"]:::top

  numerics --> core
  pricing --> numerics
  models --> pricing
  mc --> models
  smile --> pricing
  calibration --> smile
  calibration --> mc
  exotics --> smile
  exotics --> mc
  timeseries --> numerics
  risk --> pricing
  credit --> mc
  price --> calibration
  price --> mc
  portfolio --> calibration
  portfolio --> smile
  portfolio --> risk
  portfolio --> timeseries
  portfolio --> credit
```

### The portfolio application: data flow

```mermaid
flowchart LR
  classDef data fill:#fdf1e3,stroke:#c08a3e,color:#1b2430
  classDef step fill:#e8f0fe,stroke:#4a6fa5,color:#1b2430
  classDef out fill:#e9f6ee,stroke:#3a8f5d,color:#1b2430

  scenario["Scenario<br/>spot, rates, smile quotes,<br/>forwards, options, credit inputs"]:::data
  returns["Daily log returns<br/>(CSV or synthetic GARCH)"]:::data

  surface["Volatility surface<br/>smile::SmileSlice + VolSurface"]:::step
  value["Value and Greeks<br/>pricing::garman_kohlhagen_*<br/>+ forward Greeks"]:::step
  checks["Model cross-checks<br/>flat ATM vol, calibrated Heston"]:::step
  ladder["Spot ladder<br/>full revaluation vs delta-gamma"]:::step
  garch["Volatility forecast<br/>timeseries::fit_garch"]:::step
  var["VaR and ES, five methods<br/>risk::*_var_es"]:::step
  exposure["Exposure simulation<br/>credit::simulate_netting_set_values"]:::step
  cva["EE, PFE, EPE, CVA,<br/>netting benefit<br/>credit::cva_estimate"]:::step

  report["Report<br/>analyse() result"]:::out
  json["report.json"]:::out
  html["report.html<br/>(self-contained)"]:::out
  text["terminal summary"]:::out

  scenario --> surface --> value
  surface --> checks
  value --> ladder
  value --> var
  returns --> garch --> var
  surface --> exposure --> cva
  value --> report
  checks --> report
  ladder --> report
  var --> report
  cva --> report
  report --> json
  report --> html
  report --> text
```

### One run of `analyse()`

```mermaid
sequenceDiagram
  autonumber
  participant M as main
  participant A as analyse
  participant S as smile
  participant P as pricing / models
  participant T as timeseries
  participant R as risk
  participant C as credit
  M->>A: Scenario
  A->>S: SmileSlice per expiry, VolSurface
  A->>S: vol(ln(K/F), T) for each option
  A->>P: GK price and Greeks (positions, ladder)
  A->>P: calibrate_heston, heston_price (optional)
  A->>T: fit_garch, standardised_residuals
  A->>R: risk_portfolio mapped, five VaR methods
  A->>C: simulate_netting_set_values (netted and per trade)
  A->>C: exposure_profile, cva_estimate
  A-->>M: Report (or an Error naming the offending input)
  M->>M: to_text, to_json, to_html
```

## Layers and dependency rule

Each layer may depend only on layers above it in this list. Nothing depends on `apps`.

```
core          errors, typed units, day count, limits, contracts    (implemented)
numerics      adaptive quadrature (week 3), normal quantile (week 7),
              Brent root finder, Levenberg-Marquardt (week 8)      (implemented)
pricing       Garman-Kohlhagen (Greeks), implied volatility (week 8); barrier, touch and double no-touch closed forms and the contract types (week 9); Black-76 later
models        semi-analytic Heston (implemented, week 3)           (week 3)
mc            engine, random streams, GBM, Heston Euler FT and QE, variance reduction, barrier Monte Carlo with Brownian bridge  (implemented, weeks 4-5, 9)
              QE scheme, variance reduction (planned)              (week 5)
smile         delta quotes, interpolation, arbitrage checks        (implemented, week 7)
calibration   Heston smile maps, Heston calibrator                 (implemented, week 8)
exotics       Vanna-Volga, GK / Vanna-Volga / Heston comparison    (implemented, week 9)
instruments   FX forward, FX option, portfolio                     (weeks 9-11)
timeseries    EWMA, GARCH, GJR-GARCH                               (implemented, week 10)
risk          VaR, ES, portfolio methods, backtests                (implemented, week 10)
credit        exposure, netting, EE, PFE, CVA                      (implemented, week 11)
apps          fxval-price (implemented, week 6); more programs     (week 6 onward)
              not part of the public API; the only code that uses a JSON parser
examples      smile scenarios (week 7), fxval-portfolio (release 1.0): use only the public API;
              fxval-portfolio writes its JSON by hand and needs no JSON parser
```

The dependency rule will be enforced by directory-level `target_link_libraries` once the library
is split into more than one target. Until then it is a review rule.

## Rules that apply everywhere

* **Errors:** see `docs/adr/0001-error-policy.md`. Boundary functions return `Result<T>`;
  inner-loop kernels are `noexcept` and take validated types.
* **Units:** raw `double` does not cross a public API when a typed unit exists.
* **Immutability:** models and instruments are immutable after construction. Const member
  functions are safe to call from several threads at once (documented per class).
* **Determinism:** no global state, no `rand()`, explicit seeds, no fast-math. Results must not
  depend on the number of threads (test V-07).
* **Extension:** a new simulation scheme is a new class that satisfies the `PathModel` concept
  (`mc/engine.hpp`); the engine is not edited (open for extension, closed for modification).
* **Dispatch:** static dispatch (templates and concepts) inside Monte Carlo inner loops, runtime
  polymorphism only at instrument boundaries.

## Testing layout

* `tests/unit/`: behaviour of the library's own code.
* `tests/validation/`: the V-series (see `docs/test_catalogue.md`), which is evidence about the
  models and appears in the validation report.
* `tests/app/`: the command-line layer (configuration, runner, reports, CLI, metadata), and the
  allocation test (`fxval_alloc_tests`, a program of its own because it replaces `operator new`).
* `tests/regression/`: frozen results of the example configurations; checked against independent
  Python code by `python/tests/test_price_examples.py`.
* `tests/integration/run_cli.cmake`: end-to-end test of the real executable (exit codes, files,
  config hash against CMake's own SHA-256).
* `python/tests/`: data download parsers, run offline.

## Provenance

Every report written by `fxval-price` carries the git commit, the SHA-256 of the input
configuration, the random seed, the compiler and the library version (ADR 0007). The data download
script already writes a `.meta.json` next to each CSV with source URL, retrieval time and a hash.
