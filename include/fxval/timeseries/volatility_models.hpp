#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "fxval/core/error.hpp"

// Conditional volatility models for daily FX returns (docs/model_documentation.md section 9).
//
// Conventions shared by everything in this header:
//   * `returns` are log returns, assumed to have ZERO mean (daily FX: the mean is far below the
//     noise; the limitation is L-21). No mean is estimated or subtracted.
//   * A variance path v has size n + 1 for n returns: v[t] is the variance FORECAST for return t
//     using returns 0..t-1 only, and v[n] is the forecast for the next, unseen return. Nothing
//     in v[t] depends on returns[t] or later (test V-21 checks this on the full pipeline).
//   * The starting variance v[0] is the sample mean of returns^2 over the span passed in. That
//     uses the span only (never data outside it), so inside a rolling window it is look-ahead free,
//     but it does use observations later than 0 within the span to set v[0] (as the Python `arch`
//     package's backcast does, with different weights). Its influence fades like persistence^t.

namespace fxval::timeseries {

// RiskMetrics exponentially weighted moving average: v[t+1] = lambda v[t] + (1 - lambda) r[t]^2.
// Errors: invalid_argument for lambda outside (0, 1), an empty span or a non-finite return.
[[nodiscard]] Result<std::vector<double>> ewma_variance(std::span<const double> returns,
                                                        double lambda);

enum class GarchModel : std::uint8_t {
  garch,  // v[t+1] = omega + alpha r[t]^2 + beta v[t]
  gjr     // v[t+1] = omega + (alpha + gamma 1{r[t] < 0}) r[t]^2 + beta v[t]
          // (Glosten-Jagannathan-Runkle)
};

struct GarchParams {
  double omega = 0.0;  // > 0
  double alpha = 0.0;  // >= 0
  double gamma = 0.0;  // >= 0, must be 0 for the symmetric model
  double beta = 0.0;   // >= 0
  // alpha + beta + gamma / 2: the persistence of the variance (the shock is negative half the
  // time).
  [[nodiscard]] double persistence() const noexcept { return alpha + beta + 0.5 * gamma; }
  // omega / (1 - persistence), the long-run variance.
  [[nodiscard]] double unconditional_variance() const noexcept {
    return omega / (1.0 - persistence());
  }
};

// Errors: invalid_argument if a parameter is outside its range or persistence >= 1.
[[nodiscard]] Result<std::vector<double>> garch_variance_path(std::span<const double> returns,
                                                              GarchModel model,
                                                              const GarchParams& params);

// Gaussian log-likelihood sum_t -0.5 (ln 2 pi + ln v[t] + r[t]^2 / v[t]) over the n returns.
[[nodiscard]] Result<double> garch_log_likelihood(std::span<const double> returns,
                                                  GarchModel model,
                                                  const GarchParams& params);

struct GarchFitSettings {
  int starts = 4;  // starting points tried (at most 4 distinct ones exist); the best is kept
  bool standard_errors = true;
};

struct GarchFit {
  GarchModel model = GarchModel::garch;
  GarchParams params;
  double log_likelihood = 0.0;
  // From the observed information (central second differences of the log-likelihood). +infinity
  // where the matrix is not positive definite or the estimate sits at a bound.
  // Order: omega, alpha, gamma, beta (gamma is 0 and its error 0 for the symmetric model).
  std::array<double, 4> std_errors{};
  std::vector<double> variance_path;  // size n + 1, the next-day forecast is the last element
  int evaluations = 0;
  bool converged = false;  // the best start met the simplex tolerances
  std::size_t best_start = 0;
};

// Gaussian quasi-maximum-likelihood fit. The parameters are mapped to the region omega > 0,
// alpha, gamma, beta >= 0, persistence < 1 by a softmax, so every iterate is stationary; an
// optimum on the IGARCH boundary (persistence 1) is therefore approached, never reached, and shows
// up as a persistence very close to 1 (limitation L-22).
// Errors: invalid_argument for fewer than 100 returns, non-finite returns or an all-zero series;
// not_converged if no start gave a finite likelihood.
[[nodiscard]] Result<GarchFit> fit_garch(std::span<const double> returns,
                                         GarchModel model,
                                         const GarchFitSettings& settings = {});

// z[t] = returns[t] / sqrt(variance[t]) for the n returns of a variance path of size n + 1 (the
// extra last element is ignored). Errors: invalid_argument for a size mismatch or a non-positive
// variance.
[[nodiscard]] Result<std::vector<double>> standardised_residuals(
    std::span<const double> returns, std::span<const double> variance_path);

}  // namespace fxval::timeseries
