# Architecture

Status: week 2. The `core` and `pricing` layers exist. The rest is the planned structure so that
each module added later has an obvious place and an obvious set of allowed dependencies.

## Layers and dependency rule

Each layer may depend only on layers above it in this list. Nothing depends on `apps`.

```
core          errors, typed units, day count, limits, contracts    (implemented)
pricing       Garman-Kohlhagen (Greeks); Black-76 and barriers later (week 2, 9)
models        GBM, Heston, semi-analytic Heston                    (week 3)
mc            engine, schemes, random streams, thread pool         (weeks 4-5)
smile         delta quotes, interpolation, arbitrage checks        (week 7)
calibration   Heston calibrator, optimiser                         (week 8)
exotics       Vanna-Volga, barrier and touch options               (week 9)
instruments   FX forward, FX option, portfolio                     (weeks 9-11)
timeseries    EWMA, GARCH, GJR-GARCH                               (week 10)
risk          VaR, ES, backtests                                   (week 10)
credit        exposure, netting, CVA                               (week 11)
apps          command-line programs                                (week 6 onward)
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
* **Dispatch:** static dispatch (templates and concepts) inside Monte Carlo inner loops, runtime
  polymorphism only at instrument boundaries.

## Testing layout

* `tests/unit/`: behaviour of the library's own code.
* `tests/validation/`: the V-series (see `docs/test_catalogue.md`), which is evidence about the
  models and appears in the validation report.
* `tests/regression/`: frozen outputs for the example portfolios (from week 6).
* `python/tests/`: data download parsers, run offline.

## Provenance

Every output file written by an application will carry the git commit, a hash of the input
configuration, the random seed and the library version (planned for week 6). The data download
script already writes a `.meta.json` next to each CSV with source URL, retrieval time and a hash.
