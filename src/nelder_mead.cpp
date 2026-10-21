#include "fxval/numerics/nelder_mead.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace fxval::numerics {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Vertex {
  std::vector<double> x;
  double f = kInf;
};

struct Run {
  Vertex best;
  int iterations = 0;
  int evaluations = 0;
  bool converged = false;
};

[[nodiscard]] double evaluate(const ObjectiveFunction& objective,
                              const std::vector<double>& x,
                              int& evaluations) {
  ++evaluations;
  const Result<double> r = objective(x);
  if (!r || !std::isfinite(r.value())) {
    return kInf;
  }
  return r.value();
}

[[nodiscard]] Run run_simplex(const ObjectiveFunction& objective,
                              const std::vector<double>& start,
                              double start_value,
                              const NelderMeadOptions& options) {
  const std::size_t n = start.size();
  Run run;
  std::vector<Vertex> simplex(n + 1);
  simplex[0] = Vertex{start, start_value};
  for (std::size_t i = 0; i < n; ++i) {
    Vertex v{start, 0.0};
    v.x[i] += options.initial_step * std::max(std::abs(start[i]), 1.0);
    v.f = evaluate(objective, v.x, run.evaluations);
    simplex[i + 1] = std::move(v);
  }
  const auto by_value = [](const Vertex& a, const Vertex& b) {
    return a.f < b.f;
  };
  for (; run.iterations < options.max_iterations; ++run.iterations) {
    std::sort(simplex.begin(), simplex.end(), by_value);
    // Convergence: the values and the geometry have both collapsed.
    const double spread = simplex.back().f - simplex.front().f;
    double size = 0.0;
    for (std::size_t v = 1; v <= n; ++v) {
      for (std::size_t i = 0; i < n; ++i) {
        size = std::max(
            size, std::abs(simplex[v].x[i] - simplex[0].x[i]) / (1.0 + std::abs(simplex[0].x[i])));
      }
    }
    if (std::isfinite(spread) && spread <= options.f_tol && size <= options.x_tol) {
      run.converged = true;
      break;
    }
    std::vector<double> centroid(n, 0.0);
    for (std::size_t v = 0; v < n; ++v) {
      for (std::size_t i = 0; i < n; ++i) {
        centroid[i] += simplex[v].x[i] / static_cast<double>(n);
      }
    }
    const auto along = [&](double t) {
      std::vector<double> p(n);
      for (std::size_t i = 0; i < n; ++i) {
        p[i] = centroid[i] + t * (simplex[n].x[i] - centroid[i]);
      }
      return p;
    };
    Vertex reflected{along(-1.0), 0.0};
    reflected.f = evaluate(objective, reflected.x, run.evaluations);
    if (reflected.f < simplex[0].f) {
      Vertex expanded{along(-2.0), 0.0};
      expanded.f = evaluate(objective, expanded.x, run.evaluations);
      simplex[n] = expanded.f < reflected.f ? std::move(expanded) : std::move(reflected);
    } else if (reflected.f < simplex[n - 1].f) {
      simplex[n] = std::move(reflected);
    } else {
      const bool outside = reflected.f < simplex[n].f;
      Vertex contracted{along(outside ? -0.5 : 0.5), 0.0};
      contracted.f = evaluate(objective, contracted.x, run.evaluations);
      if (contracted.f < std::min(reflected.f, simplex[n].f)) {
        simplex[n] = std::move(contracted);
      } else {
        for (std::size_t v = 1; v <= n; ++v) {
          for (std::size_t i = 0; i < n; ++i) {
            simplex[v].x[i] = simplex[0].x[i] + 0.5 * (simplex[v].x[i] - simplex[0].x[i]);
          }
          simplex[v].f = evaluate(objective, simplex[v].x, run.evaluations);
        }
      }
    }
  }
  std::sort(simplex.begin(), simplex.end(), by_value);
  run.best = simplex.front();
  return run;
}

}  // namespace

Result<NelderMeadResult> nelder_mead(const ObjectiveFunction& objective,
                                     const std::vector<double>& start,
                                     const NelderMeadOptions& options) {
  if (start.empty()) {
    return Error{ErrorCode::invalid_argument, "Nelder-Mead needs at least one unknown"};
  }
  for (double v : start) {
    if (!std::isfinite(v)) {
      return Error{ErrorCode::invalid_argument, "Nelder-Mead start contains a non-finite value"};
    }
  }
  NelderMeadResult result;
  const double f0 = evaluate(objective, start, result.evaluations);
  if (!std::isfinite(f0)) {
    return Error{ErrorCode::numerical_failure, "the objective is not finite at the start point"};
  }
  result.x = start;
  result.value = f0;
  for (int restart = 0; restart <= options.max_restarts; ++restart) {
    const Run run = run_simplex(objective, result.x, result.value, options);
    result.iterations += run.iterations;
    result.evaluations += run.evaluations;
    const double gain = result.value - run.best.f;
    if (run.best.f <= result.value) {
      result.x = run.best.x;
      result.value = run.best.f;
    }
    result.converged = run.converged;
    if (gain <= options.f_tol) {
      break;
    }
  }
  return result;
}

}  // namespace fxval::numerics
