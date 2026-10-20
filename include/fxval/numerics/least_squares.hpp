#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "fxval/core/error.hpp"

// Levenberg-Marquardt least squares for small dense problems (a handful of unknowns).
//
// Minimises C(x) = 1/2 sum_i r_i(x)^2 for a residual vector r: R^n -> R^m. Method (Marquardt 1963
// scaling, Nielsen 1999 damping update, see docs/model_documentation.md section 4.8):
//
//     (J'J + lambda diag(J'J)) delta = -J'r,       x <- x + delta  if C decreases,
//
// where lambda is raised after a rejected step and lowered after an accepted one according to the
// gain ratio rho = (actual decrease) / (decrease predicted by the linear model). The Jacobian J
// comes from central finite differences (finite_difference_jacobian). m may be smaller than n:
// the step is then not unique but the damped system is still solvable, which is how the
// one-expiry identification control of test V-11 can be run at all.
//
// There are no bounds here. A caller with bounds maps its parameters to an unbounded space
// (calibration/heston_calibration.cpp does this with a logistic map).

namespace fxval::numerics {

// Residual function. Returns invalid_argument / numerical_failure for points where the model
// cannot be evaluated; the optimiser then treats the step as rejected.
using ResidualFunction = std::function<Result<std::vector<double>>(const std::vector<double>&)>;

enum class LeastSquaresTermination : std::uint8_t {
  gradient_small,  // max |J'r| <= gradient_tol
  step_small,      // |delta| <= step_tol (|x| + step_tol)
  cost_small,      // C <= cost_tol, the residuals are zero to working precision
  max_iterations,  // iteration limit reached: the result is the best point found, NOT converged
  stalled,         // damping grew without bound: no acceptable step exists from here
};

[[nodiscard]] const char* to_string(LeastSquaresTermination termination) noexcept;

struct LeastSquaresOptions {
  int max_iterations = 200;
  double gradient_tol = 1e-12;
  double step_tol = 1e-12;
  double cost_tol = 1e-26;
  double initial_damping = 1e-3;  // relative to the largest diagonal element of J'J
  double max_damping = 1e12;
  double fd_relative_step = 1e-7;
};

struct LeastSquaresResult {
  std::vector<double> x;
  std::vector<double> residuals;
  double cost = 0.0;  // 1/2 sum r^2
  int iterations = 0;
  int evaluations = 0;  // residual evaluations, including those inside Jacobians
  LeastSquaresTermination termination = LeastSquaresTermination::max_iterations;

  // True for the three termination reasons that mean "a minimum was reached".
  [[nodiscard]] bool converged() const noexcept {
    return termination == LeastSquaresTermination::gradient_small ||
           termination == LeastSquaresTermination::step_small ||
           termination == LeastSquaresTermination::cost_small;
  }
};

// Errors: invalid_argument for empty x0 or a non-finite x0; the error of the residual function if
// it fails at x0 (there is nothing to improve from a point that cannot be evaluated).
[[nodiscard]] Result<LeastSquaresResult> levenberg_marquardt(const ResidualFunction& residuals,
                                                             std::vector<double> x0,
                                                             const LeastSquaresOptions& options);

// Central-difference Jacobian, row-major m x n: J[i * n + j] = d r_i / d x_j. The step for x_j is
// relative_step * max(|x_j|, 1). `evaluations` (optional) is increased by 2 n.
[[nodiscard]] Result<std::vector<double>> finite_difference_jacobian(
    const ResidualFunction& residuals,
    const std::vector<double>& x,
    double relative_step,
    int* evaluations = nullptr);

// Standard errors sqrt(sigma2 [(J'J)^{-1}]_jj) of the n parameters from the row-major m x n
// Jacobian at the solution. A parameter that the data do not determine gets +infinity: that is the
// case whenever m <= n (fewer residuals than unknowns) or when J'J is numerically singular. The
// caller supplies the residual variance sigma2 (for example 2 C / (m - n)).
[[nodiscard]] std::vector<double> parameter_standard_errors(const std::vector<double>& jacobian,
                                                            std::size_t m,
                                                            std::size_t n,
                                                            double sigma2);

}  // namespace fxval::numerics
