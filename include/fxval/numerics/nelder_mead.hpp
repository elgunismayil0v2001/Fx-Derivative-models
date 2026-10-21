#pragma once

#include <functional>
#include <vector>

#include "fxval/core/error.hpp"

// Nelder-Mead simplex minimiser for small problems (a handful of unknowns) where a derivative is
// not available or not reliable (the GARCH likelihood: docs/model_documentation.md section 9.3).
//
// Standard coefficients: reflection 1, expansion 2, contraction 1/2, shrink 1/2. After the simplex
// has converged the search is RESTARTED from the best point with a fresh simplex, and stops only
// when a restart no longer lowers the value by more than f_tol (the usual cure for the simplex
// collapsing onto a non-stationary point). An objective that returns an error is treated as
// +infinity at that point, so the search simply walks away from it.
//
// It finds a local minimum. A caller that wants a global one runs several starts (fit_garch does).

namespace fxval::numerics {

using ObjectiveFunction = std::function<Result<double>(const std::vector<double>&)>;

struct NelderMeadOptions {
  int max_iterations = 4000;  // per simplex run, not in total
  int max_restarts = 8;
  double f_tol = 1e-13;       // spread of the function values on the simplex, and restart gain
  double x_tol = 1e-9;        // size of the simplex relative to (1 + |x|)
  double initial_step = 0.2;  // edge of the first simplex: step * max(|x_i|, 1)
};

struct NelderMeadResult {
  std::vector<double> x;
  double value = 0.0;
  int iterations = 0;
  int evaluations = 0;
  bool converged = false;  // the last run met both tolerances before its iteration limit
};

// Errors: invalid_argument for an empty start, a non-finite start or a non-finite value at the
// start; numerical_failure if the objective is infinite at the start point.
[[nodiscard]] Result<NelderMeadResult> nelder_mead(const ObjectiveFunction& objective,
                                                   const std::vector<double>& start,
                                                   const NelderMeadOptions& options = {});

}  // namespace fxval::numerics
