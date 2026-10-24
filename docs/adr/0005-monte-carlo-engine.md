# ADR 0005: Monte Carlo engine

* Status: accepted
* Date: 2026-10-14

## Context

Monte Carlo is used for Heston prices, barrier options, VaR scenarios
and exposure for CVA. The engine must be correct, fast, extensible without
being edited, and reproducible: the same seed must give the same answer on a laptop with 4 cores
and a server with 64 (test V-07).

## Decision

* **Counter-based random numbers (Philox4x32-10).** Output is a pure function of (seed, stream,
  block). Path `p` uses stream `p`, so a path's numbers never depend on threads or scheduling.
  No generator object is shared and none has to be "split" or "jumped ahead".
* **Fixed blocks, merge in block order.** Floating-point addition is not associative, so the
  order of the sum must not depend on the thread count. Paths are grouped in blocks of fixed
  size; each block is summarised independently (Welford); summaries are merged in block order
  (Chan et al.). Threads only decide *who* computes a block, never *how* it is combined.
* **Static dispatch through a `PathModel` concept.** `run_monte_carlo<Model, Payoff>` is a
  template; a model provides `simulate_terminal_spot(NormalStream&, steps) const noexcept`.
  A new scheme (QE, Brownian-bridge barrier) is a new class and no existing file changes.
  There is no virtual call per path.
* **Errors as values at the boundary.** Configuration problems are `invalid_argument`; a
  non-finite estimate is `numerical_failure`, never a NaN returned as a number (ADR 0001).
* **`std::jthread` plus an atomic block counter.** The calling thread works too, so a refused
  thread creation only lowers parallelism and cannot change the result.
* **Box-Muller, not inverse CDF.** No approximation formula that would need its own validation;
  the cost is one `log`, `sqrt`, `cos`, `sin` per two normals.

## Alternatives considered

* `std::mt19937_64` per thread with seeding by thread id: results depend on how paths are split
  among threads, which is exactly what V-07 forbids.
* One generator, skip-ahead per block (e.g. PCG or xoshiro jump): works, but needs a correct
  jump polynomial and ties streams to block boundaries, so the block size would change the
  random numbers of a path.
* Sobol sequences: faster convergence for smooth payoffs. Not in v1.0 scope; the stream
  interface would have to change (dimension ordering), so it is recorded as a later option.
* Runtime polymorphism (`virtual` model interface): simpler to read, costs an indirect call per
  path and blocks inlining of the step loop.

## Consequences

* V-07 can demand bitwise equality, and the test includes a control showing a naive design fails it.
* Changing the block size changes the last digits (different summation order), not the
  distribution. This is documented and tested (agreement to 1e-12).
* The payoff sees only the terminal spot. Path-dependent products extend the concept (the barrier Monte Carlo reports each step to an observer).
* Normals are truncated at about 8.6 standard deviations (inherent in 53-bit uniforms with
  Box-Muller); irrelevant for the tails used in this project, but stated.
