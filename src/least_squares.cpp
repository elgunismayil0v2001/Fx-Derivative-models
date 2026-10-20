#include "fxval/numerics/least_squares.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fxval::numerics {
namespace {

using Vector = std::vector<double>;

[[nodiscard]] bool all_finite(const Vector& v) {
  return std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
}

[[nodiscard]] double half_sum_squares(const Vector& r) {
  double sum = 0.0;
  for (double value : r) {
    sum += value * value;
  }
  return 0.5 * sum;
}

// In-place Cholesky factorisation A = L L' of a symmetric n x n matrix (row-major, lower triangle
// used). Returns false if a pivot is not safely positive.
[[nodiscard]] bool cholesky(Vector& a, std::size_t n, const Vector& reference_diagonal) {
  for (std::size_t j = 0; j < n; ++j) {
    double pivot = a[j * n + j];
    for (std::size_t k = 0; k < j; ++k) {
      pivot -= a[j * n + k] * a[j * n + k];
    }
    if (!(pivot > 1e-13 * reference_diagonal[j]) || !std::isfinite(pivot)) {
      return false;
    }
    const double root = std::sqrt(pivot);
    a[j * n + j] = root;
    for (std::size_t i = j + 1; i < n; ++i) {
      double value = a[i * n + j];
      for (std::size_t k = 0; k < j; ++k) {
        value -= a[i * n + k] * a[j * n + k];
      }
      a[i * n + j] = value / root;
    }
  }
  return true;
}

void cholesky_solve(const Vector& l, std::size_t n, Vector& b) {
  for (std::size_t i = 0; i < n; ++i) {  // L y = b
    double value = b[i];
    for (std::size_t k = 0; k < i; ++k) {
      value -= l[i * n + k] * b[k];
    }
    b[i] = value / l[i * n + i];
  }
  for (std::size_t ii = n; ii-- > 0;) {  // L' x = y
    double value = b[ii];
    for (std::size_t k = ii + 1; k < n; ++k) {
      value -= l[k * n + ii] * b[k];
    }
    b[ii] = value / l[ii * n + ii];
  }
}

struct NormalEquations {
  Vector jtj;       // n x n
  Vector gradient;  // J' r
};

[[nodiscard]] NormalEquations build_normal_equations(const Vector& jacobian,
                                                     const Vector& r,
                                                     std::size_t m,
                                                     std::size_t n) {
  NormalEquations ne{Vector(n * n, 0.0), Vector(n, 0.0)};
  for (std::size_t i = 0; i < m; ++i) {
    for (std::size_t a = 0; a < n; ++a) {
      const double ja = jacobian[i * n + a];
      ne.gradient[a] += ja * r[i];
      for (std::size_t b = 0; b <= a; ++b) {
        ne.jtj[a * n + b] += ja * jacobian[i * n + b];
      }
    }
  }
  for (std::size_t a = 0; a < n; ++a) {
    for (std::size_t b = a + 1; b < n; ++b) {
      ne.jtj[a * n + b] = ne.jtj[b * n + a];
    }
  }
  return ne;
}

}  // namespace

const char* to_string(LeastSquaresTermination termination) noexcept {
  switch (termination) {
  case LeastSquaresTermination::gradient_small:
    return "gradient_small";
  case LeastSquaresTermination::step_small:
    return "step_small";
  case LeastSquaresTermination::cost_small:
    return "cost_small";
  case LeastSquaresTermination::max_iterations:
    return "max_iterations";
  case LeastSquaresTermination::stalled:
    return "stalled";
  }
  return "unknown";
}

Result<Vector> finite_difference_jacobian(const ResidualFunction& residuals,
                                          const Vector& x,
                                          double relative_step,
                                          int* evaluations) {
  const std::size_t n = x.size();
  Vector jacobian;
  std::size_t m = 0;
  Vector probe = x;
  for (std::size_t j = 0; j < n; ++j) {
    const double h = relative_step * std::max(std::abs(x[j]), 1.0);
    probe[j] = x[j] + h;
    const Result<Vector> up = residuals(probe);
    probe[j] = x[j] - h;
    const Result<Vector> down = residuals(probe);
    probe[j] = x[j];
    if (evaluations != nullptr) {
      *evaluations += 2;
    }
    if (!up) {
      return up.error();
    }
    if (!down) {
      return down.error();
    }
    if (j == 0) {
      m = up.value().size();
      jacobian.assign(m * n, 0.0);
    }
    if (up.value().size() != m || down.value().size() != m) {
      return Error{ErrorCode::invalid_argument, "residual vector changed length"};
    }
    for (std::size_t i = 0; i < m; ++i) {
      jacobian[i * n + j] = (up.value()[i] - down.value()[i]) / (2.0 * h);
    }
  }
  if (!all_finite(jacobian)) {
    return Error{ErrorCode::numerical_failure, "Jacobian contains a non-finite value"};
  }
  return jacobian;
}

std::vector<double> parameter_standard_errors(const Vector& jacobian,
                                              std::size_t m,
                                              std::size_t n,
                                              double sigma2) {
  Vector infinite(n, std::numeric_limits<double>::infinity());
  if (m <= n || n == 0 || jacobian.size() != m * n || !std::isfinite(sigma2) || !(sigma2 >= 0.0)) {
    return infinite;
  }
  const NormalEquations ne = build_normal_equations(jacobian, Vector(m, 0.0), m, n);
  Vector diagonal(n);
  for (std::size_t j = 0; j < n; ++j) {
    diagonal[j] = ne.jtj[j * n + j];
  }
  Vector factor = ne.jtj;
  if (!cholesky(factor, n, diagonal)) {
    return infinite;
  }
  Vector errors(n);
  for (std::size_t j = 0; j < n; ++j) {
    Vector unit(n, 0.0);
    unit[j] = 1.0;
    cholesky_solve(factor, n, unit);  // column j of (J'J)^{-1}
    errors[j] = std::sqrt(sigma2 * unit[j]);
  }
  return errors;
}

Result<LeastSquaresResult> levenberg_marquardt(const ResidualFunction& residuals,
                                               Vector x0,
                                               const LeastSquaresOptions& options) {
  const std::size_t n = x0.size();
  if (n == 0 || !all_finite(x0)) {
    return Error{ErrorCode::invalid_argument, "least squares needs a finite, non-empty start"};
  }
  LeastSquaresResult result;
  result.x = std::move(x0);
  Result<Vector> r0 = residuals(result.x);
  ++result.evaluations;
  if (!r0) {
    return r0.error();
  }
  if (!all_finite(r0.value()) || r0.value().empty()) {
    return Error{ErrorCode::numerical_failure, "residuals at the start are empty or not finite"};
  }
  result.residuals = std::move(r0).value();
  result.cost = half_sum_squares(result.residuals);
  const std::size_t m = result.residuals.size();

  double lambda = -1.0;  // set from the first J'J
  double nu = 2.0;
  for (int iteration = 0; iteration < options.max_iterations; ++iteration) {
    result.iterations = iteration;
    if (result.cost <= options.cost_tol) {
      result.termination = LeastSquaresTermination::cost_small;
      return result;
    }
    const Result<Vector> jacobian = finite_difference_jacobian(
        residuals, result.x, options.fd_relative_step, &result.evaluations);
    if (!jacobian) {
      return jacobian.error();
    }
    const NormalEquations ne = build_normal_equations(jacobian.value(), result.residuals, m, n);
    double largest_diagonal = 0.0;
    Vector scale(n);
    for (std::size_t j = 0; j < n; ++j) {
      largest_diagonal = std::max(largest_diagonal, ne.jtj[j * n + j]);
    }
    for (std::size_t j = 0; j < n; ++j) {
      // Marquardt scaling, floored so that an insensitive parameter still gets damped.
      scale[j] = std::max(ne.jtj[j * n + j], 1e-12 * std::max(largest_diagonal, 1e-300));
    }
    if (lambda < 0.0) {
      lambda = options.initial_damping;
    }
    double gradient_norm = 0.0;
    for (double g : ne.gradient) {
      gradient_norm = std::max(gradient_norm, std::abs(g));
    }
    if (gradient_norm <= options.gradient_tol) {
      result.termination = LeastSquaresTermination::gradient_small;
      return result;
    }

    bool accepted = false;
    while (!accepted) {
      if (lambda > options.max_damping) {
        result.termination = LeastSquaresTermination::stalled;
        return result;
      }
      Vector system = ne.jtj;
      for (std::size_t j = 0; j < n; ++j) {
        system[j * n + j] += lambda * scale[j];
      }
      if (!cholesky(system, n, scale)) {
        lambda *= nu;
        nu *= 2.0;
        continue;
      }
      Vector step(n);
      for (std::size_t j = 0; j < n; ++j) {
        step[j] = -ne.gradient[j];
      }
      cholesky_solve(system, n, step);

      double step_norm = 0.0;
      double x_norm = 0.0;
      Vector trial = result.x;
      for (std::size_t j = 0; j < n; ++j) {
        trial[j] += step[j];
        step_norm += step[j] * step[j];
        x_norm += result.x[j] * result.x[j];
      }
      step_norm = std::sqrt(step_norm);
      x_norm = std::sqrt(x_norm);
      if (step_norm <= options.step_tol * (x_norm + options.step_tol)) {
        result.termination = LeastSquaresTermination::step_small;
        return result;
      }

      const Result<Vector> trial_residuals = residuals(trial);
      ++result.evaluations;
      double trial_cost = std::numeric_limits<double>::infinity();
      if (trial_residuals && all_finite(trial_residuals.value()) &&
          trial_residuals.value().size() == m) {
        trial_cost = half_sum_squares(trial_residuals.value());
      }
      // Decrease predicted by the linear model: 1/2 delta'(lambda D delta - J'r).
      double predicted = 0.0;
      for (std::size_t j = 0; j < n; ++j) {
        predicted += step[j] * (lambda * scale[j] * step[j] - ne.gradient[j]);
      }
      predicted *= 0.5;
      const double actual = result.cost - trial_cost;
      if (std::isfinite(trial_cost) && predicted > 0.0 && actual > 0.0) {
        const double rho = actual / predicted;
        const double factor = 1.0 - (2.0 * rho - 1.0) * (2.0 * rho - 1.0) * (2.0 * rho - 1.0);
        lambda *= std::max(1.0 / 3.0, factor);
        nu = 2.0;
        result.x = std::move(trial);
        result.residuals = trial_residuals.value();
        result.cost = trial_cost;
        accepted = true;
      } else {
        lambda *= nu;
        nu *= 2.0;
      }
    }
  }
  result.iterations = options.max_iterations;
  result.termination = LeastSquaresTermination::max_iterations;
  return result;
}

}  // namespace fxval::numerics
