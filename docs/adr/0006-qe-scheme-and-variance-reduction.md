# ADR 0006: QE scheme and variance reduction

* Status: accepted
* Date: 2026-10-16

## Context

Week 4 left Euler with full truncation as the only Heston scheme. It is simple and badly biased
when the Feller condition fails (V-05: 7.8 standard errors left at 128 steps). FX Heston fits
often violate Feller, so a production-style simulator needs a scheme that is accurate with few
steps (CVA exposure and VaR scenarios use 10 to 50 steps). Separately, the standard error of a
plain estimator falls as `1/sqrt(N)`; for a pricing library, antithetic and control variates are
the first tools expected.

## Decisions

* **Andersen's QE scheme (2008), central discretisation of `ln S`, with the martingale
  correction**, as a new class `HestonQuadraticExponential`. It satisfies `PathModel`, so
  `run_monte_carlo` accepted it without any change (open for extension). Switching level
  `psi_c = 1.5` is a parameter in `[1, 2]`.
* **One Philox block per step.** The exponential branch needs a uniform; it is taken as
  `U = Phi(Z_v)` with `erfc`, and `1 - U` is computed directly from `erfc` so the right tail keeps
  its precision. This keeps the stream layout identical to Euler (two normals per step) and avoids
  a second block or a second kind of draw. Cost: one `erfc` in the exponential branch.
* **Fallback instead of failure.** The martingale correction needs a moment `E[exp(A v')]` that
  does not exist for very long steps with large positive `rho`. A path function cannot return an
  error (`noexcept`, inner loop), so in that corner the uncorrected constant `K0` is used.
  A random search over wide parameter ranges found it only for time steps of about 4 years or
  more; the QE reference table contains such rows so both code paths are checked. `xi > 0` is
  required (`invalid_argument` otherwise); for `xi = 0` the exact GBM model applies.
* **The engine gained one function, `run_samples`**, the old `run_monte_carlo` body with the path
  index as the only input. `run_monte_carlo` now calls it. This is a refactor with no change of
  behaviour (all week 4 tests, including the bitwise V-07 checks, pass unchanged). Antithetic and
  control variate estimators are built on `run_samples`, so they inherit the block and merge-order
  design and reproducibility for any thread count.
* **Antithetic variates by a sign flip in the stream** (`NormalStream(seed, stream, true)` returns
  the negated normals). Exact (only the sign bit changes), no change to any model, and it works for
  QE because `U = Phi(z)` becomes `1 - U`. `McConfig.paths` then counts pairs; fair comparisons are
  at equal cost (V-08).
* **Control variate coefficient from a pilot run on separate streams** (`pilot_stream_offset`,
  2^62): a coefficient estimated from the same paths is random and correlated with the estimate
  (bias of order `1/n`). The pilot is single threaded, hence deterministic.
* **The control is `g(S_T) = S_T` with mean equal to the forward**, the one control that is exact
  for every Heston scheme here. A Garman-Kohlhagen price is a valid control only under GBM, where it
  is checked (V-08 d); it is not a valid control for Heston paths (its expectation under Heston is
  not the GK price). The API takes any control function with a known mean.
* **Benchmarks are programs, not tests** (`bench/`, off by default, not in `ctest`): timings depend
  on the machine and must never gate a build.

## Alternatives considered

* **Exact simulation (Broadie-Kaya):** unbiased, but needs the non-central chi-square sampler and a
  Fourier inversion of the integrated variance per step; slower and more code than QE for no
  practical gain at the accuracy needed.
* **Moment-matched or "Euler with absorption/reflection" variants:** simpler but with the same
  Feller-related bias as full truncation (Lord et al. 2010 compare them).
* **Sobol quasi-random numbers:** better convergence for smooth payoffs; deferred, because it
  changes the stream interface (ADR 0005).
* **Control coefficient from the same paths:** simpler, small bias, and the standard error formula
  would need a correction; rejected.
* **Antithetic variates as a model wrapper:** impossible without changing what the engine sees, since
  the average is taken over payoffs, not over spots.

## Consequences

* QE is the recommended scheme; Euler stays as the baseline that V-06 compares against.
* The independent Python path table for QE (`gen_qe_reference.py`) was written by the same author
  from the same paper; a misreading of the paper would be in both. V-06 (semi-analytic prices) and
  the exact-moment unit tests of the variance step are the checks that do not share that risk.
* A wrong control mean cannot be detected from the standard error; V-08 contains a control with a
  deliberately wrong mean that must be rejected, to show the check can fail.
