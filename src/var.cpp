#include "fxval/risk/var.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "fxval/numerics/normal.hpp"

namespace fxval::risk {
namespace {

[[nodiscard]] bool valid_level(double level) noexcept {
  return level > 0.0 && level < 1.0;
}

[[nodiscard]] Result<double> tail_quantile(double level) {
  if (!valid_level(level)) {
    return Error{ErrorCode::invalid_argument,
                 "the confidence level must be strictly inside (0, 1)"};
  }
  return numerics::normal_quantile(1.0 - level);
}

}  // namespace

Result<VarEs> historical_var_es(std::span<const double> pnl, double level) {
  if (pnl.empty()) {
    return Error{ErrorCode::invalid_argument, "no P&L scenarios"};
  }
  if (!valid_level(level)) {
    return Error{ErrorCode::invalid_argument,
                 "the confidence level must be strictly inside (0, 1)"};
  }
  std::vector<double> sorted(pnl.begin(), pnl.end());
  for (double x : sorted) {
    if (!std::isfinite(x)) {
      return Error{ErrorCode::invalid_argument, "a P&L scenario is not finite"};
    }
  }
  const auto n = static_cast<double>(sorted.size());
  // ceil(n q) with a guard for products such as 0.01 * 100 that land a rounding error above 1.
  const double raw = n * (1.0 - level);
  auto k = static_cast<std::size_t>(std::ceil(raw - 1e-9));
  k = std::clamp<std::size_t>(k, 1, sorted.size());
  std::nth_element(
      sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(k - 1), sorted.end());
  const double kth = sorted[k - 1];
  double tail_sum = 0.0;
  for (std::size_t i = 0; i < k; ++i) {
    tail_sum += sorted[i];  // after nth_element the k smallest are in the first k slots
  }
  const VarEs out{-kth, -tail_sum / static_cast<double>(k)};
  if (!std::isfinite(out.es)) {
    return Error{ErrorCode::numerical_failure,
                 "the tail sum overflowed; rescale the P&L scenarios"};
  }
  return out;
}

Result<VarEs> normal_var_es(double mean, double sd, double level) {
  if (!std::isfinite(mean) || !std::isfinite(sd) || sd < 0.0) {
    return Error{ErrorCode::invalid_argument,
                 "normal VaR needs a finite mean and a finite sd >= 0"};
  }
  const Result<double> z = tail_quantile(level);
  if (!z) {
    return z.error();
  }
  const double q = 1.0 - level;
  const VarEs out{-(mean + sd * z.value()), -mean + sd * numerics::normal_pdf(z.value()) / q};
  if (!std::isfinite(out.var) || !std::isfinite(out.es)) {
    return Error{ErrorCode::numerical_failure, "the normal VaR or ES overflowed"};
  }
  return out;
}

Result<double> cornish_fisher_var(
    double mean, double sd, double skewness, double excess_kurtosis, double level) {
  if (!std::isfinite(mean) || !std::isfinite(sd) || sd < 0.0 || !std::isfinite(skewness) ||
      !std::isfinite(excess_kurtosis)) {
    return Error{ErrorCode::invalid_argument,
                 "Cornish-Fisher VaR needs finite moments and sd >= 0"};
  }
  const Result<double> zr = tail_quantile(level);
  if (!zr) {
    return zr.error();
  }
  const double z = zr.value();
  const double s = skewness;
  const double k = excess_kurtosis;
  const double z_cf = z + (z * z - 1.0) * s / 6.0 + (z * z * z - 3.0 * z) * k / 24.0 -
                      (2.0 * z * z * z - 5.0 * z) * s * s / 36.0;
  const double var = -(mean + sd * z_cf);
  if (!std::isfinite(var)) {
    return Error{ErrorCode::numerical_failure, "the Cornish-Fisher VaR overflowed"};
  }
  return var;
}

}  // namespace fxval::risk
