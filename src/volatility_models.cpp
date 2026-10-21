#include "fxval/timeseries/volatility_models.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "fxval/numerics/nelder_mead.hpp"

namespace fxval::timeseries {
namespace {

[[nodiscard]] bool is_decay(double lambda) noexcept {
  return lambda > 0.0 && lambda < 1.0;
}

constexpr double kInf = std::numeric_limits<double>::infinity();

[[nodiscard]] Result<double> mean_square(std::span<const double> returns) {
  if (returns.empty()) {
    return Error{ErrorCode::invalid_argument, "the return series is empty"};
  }
  double sum = 0.0;
  for (double r : returns) {
    if (!std::isfinite(r)) {
      return Error{ErrorCode::invalid_argument, "the return series contains a non-finite value"};
    }
    sum += r * r;
  }
  const double ms = sum / static_cast<double>(returns.size());
  if (!(ms > 0.0)) {
    return Error{ErrorCode::invalid_argument, "the return series is identically zero"};
  }
  return ms;
}

[[nodiscard]] Result<bool> check_params(GarchModel model, const GarchParams& p) {
  if (!(p.omega > 0.0) || !std::isfinite(p.omega) || !(p.alpha >= 0.0) || !(p.beta >= 0.0) ||
      !(p.gamma >= 0.0) || !std::isfinite(p.alpha) || !std::isfinite(p.beta) ||
      !std::isfinite(p.gamma)) {
    return Error{ErrorCode::invalid_argument,
                 "GARCH parameters need omega > 0 and alpha, beta, gamma >= 0"};
  }
  if (model == GarchModel::garch && p.gamma != 0.0) {
    return Error{ErrorCode::invalid_argument, "the symmetric GARCH model has gamma = 0"};
  }
  if (!(p.persistence() < 1.0)) {
    return Error{ErrorCode::invalid_argument, "persistence alpha + beta + gamma/2 must be below 1"};
  }
  return true;
}

// The recursion, without checks. Returns the log-likelihood and fills `path` when not null.
[[nodiscard]] double run_recursion(std::span<const double> returns,
                                   GarchModel model,
                                   const GarchParams& p,
                                   double v0,
                                   std::vector<double>* path) {
  double v = v0;
  double ll = 0.0;
  if (path != nullptr) {
    path->assign(1, v);
    path->reserve(returns.size() + 1);
  }
  const double log_two_pi = std::log(2.0 * std::numbers::pi);
  for (double r : returns) {
    ll += -0.5 * (log_two_pi + std::log(v) + r * r / v);
    const double a = (model == GarchModel::gjr && r < 0.0) ? p.alpha + p.gamma : p.alpha;
    v = p.omega + a * r * r + p.beta * v;
    if (path != nullptr) {
      path->push_back(v);
    }
  }
  return ll;
}

// Softmax map between the unconstrained vector and (omega, alpha, gamma, beta).
//   u = (ln omega, u_alpha, [u_h,] u_beta); D = 1 + sum exp(u_*); alpha = exp(u_alpha) / D, ...
//   gamma = 2 h for the asymmetric model; persistence = 1 - 1/D.
[[nodiscard]] Result<GarchParams> to_params(GarchModel model, const std::vector<double>& u) {
  for (double x : u) {
    if (!std::isfinite(x) || std::abs(x) > 60.0) {
      return Error{ErrorCode::numerical_failure, "unconstrained GARCH parameter out of range"};
    }
  }
  GarchParams p;
  p.omega = std::exp(u[0]);
  if (model == GarchModel::garch) {
    const double ea = std::exp(u[1]);
    const double eb = std::exp(u[2]);
    const double d = 1.0 + ea + eb;
    p.alpha = ea / d;
    p.beta = eb / d;
  } else {
    const double ea = std::exp(u[1]);
    const double eh = std::exp(u[2]);
    const double eb = std::exp(u[3]);
    const double d = 1.0 + ea + eh + eb;
    p.alpha = ea / d;
    p.gamma = 2.0 * eh / d;
    p.beta = eb / d;
  }
  return p;
}

[[nodiscard]] std::vector<double> to_unconstrained(
    GarchModel model, double omega, double alpha, double h, double beta) {
  const double slack = 1.0 - alpha - h - beta;
  std::vector<double> u{std::log(omega), std::log(alpha / slack)};
  if (model == GarchModel::gjr) {
    u.push_back(std::log(h / slack));
  }
  u.push_back(std::log(beta / slack));
  return u;
}

// Negative log-likelihood Hessian by central differences, in the constrained parameters.
[[nodiscard]] std::array<double, 4> standard_errors(std::span<const double> returns,
                                                    GarchModel model,
                                                    const GarchParams& p,
                                                    double v0) {
  const std::size_t k = model == GarchModel::garch ? 3 : 4;  // omega, alpha, [gamma,] beta
  const auto get = [&](const GarchParams& q, std::size_t i) -> double {
    if (i == 0) {
      return q.omega;
    }
    if (i == 1) {
      return q.alpha;
    }
    if (model == GarchModel::gjr && i == 2) {
      return q.gamma;
    }
    return q.beta;
  };
  const auto set = [&](GarchParams q, std::size_t i, double value) {
    if (i == 0) {
      q.omega = value;
    } else if (i == 1) {
      q.alpha = value;
    } else if (model == GarchModel::gjr && i == 2) {
      q.gamma = value;
    } else {
      q.beta = value;
    }
    return q;
  };
  const auto f = [&](const GarchParams& q) {
    if (!(q.omega > 0.0) || q.alpha < 0.0 || q.beta < 0.0 || q.gamma < 0.0 ||
        !(q.persistence() < 1.0)) {
      return kInf;
    }
    return -run_recursion(returns, model, q, v0, nullptr);
  };
  std::array<double, 4> out{kInf, kInf, kInf, kInf};
  if (model == GarchModel::garch) {
    out[2] = 0.0;
  }
  std::array<std::array<double, 4>, 4> h{};
  std::array<double, 4> step{};
  for (std::size_t i = 0; i < k; ++i) {
    step[i] = 2e-4 * std::max(get(p, i), 1e-8);
  }
  const double f0 = f(p);
  for (std::size_t i = 0; i < k; ++i) {
    for (std::size_t j = i; j < k; ++j) {
      double value = 0.0;
      if (i == j) {
        value = (f(set(p, i, get(p, i) + step[i])) - 2.0 * f0 + f(set(p, i, get(p, i) - step[i]))) /
                (step[i] * step[i]);
      } else {
        const GarchParams pp = set(set(p, i, get(p, i) + step[i]), j, get(p, j) + step[j]);
        const GarchParams pm = set(set(p, i, get(p, i) + step[i]), j, get(p, j) - step[j]);
        const GarchParams mp = set(set(p, i, get(p, i) - step[i]), j, get(p, j) + step[j]);
        const GarchParams mm = set(set(p, i, get(p, i) - step[i]), j, get(p, j) - step[j]);
        value = (f(pp) - f(pm) - f(mp) + f(mm)) / (4.0 * step[i] * step[j]);
      }
      h[i][j] = h[j][i] = value;
    }
  }
  // Invert by Gauss-Jordan with partial pivoting; any failure leaves the errors infinite.
  std::array<std::array<double, 8>, 4> a{};
  for (std::size_t i = 0; i < k; ++i) {
    for (std::size_t j = 0; j < k; ++j) {
      if (!std::isfinite(h[i][j])) {
        return out;
      }
      a[i][j] = h[i][j];
    }
    a[i][k + i] = 1.0;
  }
  for (std::size_t c = 0; c < k; ++c) {
    std::size_t best = c;
    for (std::size_t r = c + 1; r < k; ++r) {
      if (std::abs(a[r][c]) > std::abs(a[best][c])) {
        best = r;
      }
    }
    if (!(std::abs(a[best][c]) > 1e-300)) {
      return out;
    }
    std::swap(a[c], a[best]);
    const double pivot = a[c][c];
    for (std::size_t j = 0; j < 2 * k; ++j) {
      a[c][j] /= pivot;
    }
    for (std::size_t r = 0; r < k; ++r) {
      if (r != c) {
        const double factor = a[r][c];
        for (std::size_t j = 0; j < 2 * k; ++j) {
          a[r][j] -= factor * a[c][j];
        }
      }
    }
  }
  std::array<double, 4> se{};
  for (std::size_t i = 0; i < k; ++i) {
    const double var = a[i][k + i];
    if (!(var > 0.0) || !std::isfinite(var)) {
      return out;  // not positive definite: the data do not determine this parameter
    }
    se[i] = std::sqrt(var);
  }
  if (model == GarchModel::garch) {
    return {se[0], se[1], 0.0, se[2]};
  }
  return {se[0], se[1], se[2], se[3]};
}

}  // namespace

Result<std::vector<double>> ewma_variance(std::span<const double> returns, double lambda) {
  if (!is_decay(lambda)) {
    return Error{ErrorCode::invalid_argument, "the EWMA decay lambda must be in (0, 1)"};
  }
  const Result<double> v0 = mean_square(returns);
  if (!v0) {
    return v0.error();
  }
  std::vector<double> v;
  v.reserve(returns.size() + 1);
  v.push_back(v0.value());
  for (double r : returns) {
    v.push_back(lambda * v.back() + (1.0 - lambda) * r * r);
  }
  return v;
}

Result<std::vector<double>> garch_variance_path(std::span<const double> returns,
                                                GarchModel model,
                                                const GarchParams& params) {
  const Result<bool> ok = check_params(model, params);
  if (!ok) {
    return ok.error();
  }
  const Result<double> v0 = mean_square(returns);
  if (!v0) {
    return v0.error();
  }
  std::vector<double> path;
  static_cast<void>(run_recursion(returns, model, params, v0.value(), &path));
  return path;
}

Result<double> garch_log_likelihood(std::span<const double> returns,
                                    GarchModel model,
                                    const GarchParams& params) {
  const Result<bool> ok = check_params(model, params);
  if (!ok) {
    return ok.error();
  }
  const Result<double> v0 = mean_square(returns);
  if (!v0) {
    return v0.error();
  }
  const double ll = run_recursion(returns, model, params, v0.value(), nullptr);
  if (!std::isfinite(ll)) {
    return Error{ErrorCode::numerical_failure, "the log-likelihood is not finite"};
  }
  return ll;
}

Result<GarchFit> fit_garch(std::span<const double> returns,
                           GarchModel model,
                           const GarchFitSettings& settings) {
  if (returns.size() < 100) {
    return Error{ErrorCode::invalid_argument, "a GARCH fit needs at least 100 returns"};
  }
  const Result<double> v0r = mean_square(returns);
  if (!v0r) {
    return v0r.error();
  }
  const double v0 = v0r.value();

  const numerics::ObjectiveFunction objective =
      [&](const std::vector<double>& u) -> Result<double> {
    const Result<GarchParams> p = to_params(model, u);
    if (!p) {
      return p.error();
    }
    const double ll = run_recursion(returns, model, p.value(), v0, nullptr);
    if (!std::isfinite(ll)) {
      return Error{ErrorCode::numerical_failure, "non-finite likelihood"};
    }
    return -ll;
  };

  struct Start {
    double alpha, h, beta;
  };
  const std::array<Start, 4> starts{Start{0.05, 0.03, 0.90},
                                    Start{0.10, 0.05, 0.80},
                                    Start{0.02, 0.01, 0.95},
                                    Start{0.15, 0.08, 0.60}};
  const std::size_t n_starts =
      std::clamp<std::size_t>(static_cast<std::size_t>(std::max(settings.starts, 1)), 1, 4);

  GarchFit best;
  bool have = false;
  best.model = model;
  int evaluations = 0;
  for (std::size_t s = 0; s < n_starts; ++s) {
    const Start st = starts[s];
    const double h = model == GarchModel::gjr ? st.h : 0.0;
    const double slack = 1.0 - st.alpha - h - st.beta;
    const std::vector<double> u0 = to_unconstrained(model, v0 * slack, st.alpha, h, st.beta);
    const Result<numerics::NelderMeadResult> r = numerics::nelder_mead(objective, u0);
    if (!r) {
      continue;
    }
    evaluations += r.value().evaluations;
    const double ll = -r.value().value;
    if (!have || ll > best.log_likelihood) {
      const Result<GarchParams> p = to_params(model, r.value().x);
      if (!p) {
        continue;
      }
      have = true;
      best.params = p.value();
      best.log_likelihood = ll;
      best.converged = r.value().converged;
      best.best_start = s;
    }
  }
  if (!have) {
    return Error{ErrorCode::not_converged, "no starting point gave a finite GARCH likelihood"};
  }
  best.evaluations = evaluations;
  static_cast<void>(run_recursion(returns, model, best.params, v0, &best.variance_path));
  if (settings.standard_errors) {
    best.std_errors = standard_errors(returns, model, best.params, v0);
  } else {
    best.std_errors = {kInf, kInf, kInf, kInf};
  }
  return best;
}

Result<std::vector<double>> standardised_residuals(std::span<const double> returns,
                                                   std::span<const double> variance_path) {
  if (variance_path.size() != returns.size() + 1) {
    return Error{ErrorCode::invalid_argument,
                 "the variance path must have one more element than the returns"};
  }
  std::vector<double> z;
  z.reserve(returns.size());
  for (std::size_t t = 0; t < returns.size(); ++t) {
    if (!(variance_path[t] > 0.0) || !std::isfinite(variance_path[t])) {
      return Error{ErrorCode::invalid_argument,
                   "a conditional variance is not positive and finite"};
    }
    z.push_back(returns[t] / std::sqrt(variance_path[t]));
  }
  return z;
}

}  // namespace fxval::timeseries
