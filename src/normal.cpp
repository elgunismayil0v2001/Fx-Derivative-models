#include "fxval/numerics/normal.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fxval::numerics {
namespace {

// Peter Acklam's rational approximation of the normal quantile for 0 < q <= 0.5. The coefficients
// are checked against SciPy in tests/unit/test_normal.cpp and in the smile reference table; the
// Halley step below makes the final value independent of the approximation's own error.
constexpr std::array<double, 6> central_num{-3.969683028665376e+01,
                                            2.209460984245205e+02,
                                            -2.759285104469687e+02,
                                            1.383577518672690e+02,
                                            -3.066479806614716e+01,
                                            2.506628277459239e+00};
constexpr std::array<double, 5> central_den{-5.447609879822406e+01,
                                            1.615858368580409e+02,
                                            -1.556989798598866e+02,
                                            6.680131188771972e+01,
                                            -1.328068155288572e+01};
constexpr std::array<double, 6> tail_num{-7.784894002430293e-03,
                                         -3.223964580411365e-01,
                                         -2.400758277161838e+00,
                                         -2.549732539343734e+00,
                                         4.374664141464968e+00,
                                         2.938163982698783e+00};
constexpr std::array<double, 4> tail_den{
    7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00};
constexpr double tail_start = 0.02425;

// Polynomial with the given coefficients, highest power first, by Horner's rule.
template <std::size_t N>
constexpr double horner(const std::array<double, N>& coefficients, double t) noexcept {
  double sum = coefficients[0];
  for (std::size_t i = 1; i < N; ++i) {
    sum = sum * t + coefficients[i];
  }
  return sum;
}

// Quantile of the lower tail, 0 < q <= 0.5. The result is <= 0.
double lower_tail_quantile(double q) noexcept {
  double x = 0.0;
  if (q < tail_start) {
    const double t = std::sqrt(-2.0 * std::log(q));
    x = horner(tail_num, t) / (horner(tail_den, t) * t + 1.0);
  } else {
    const double t = q - 0.5;
    const double r = t * t;
    x = horner(central_num, r) * t / (horner(central_den, r) * r + 1.0);
  }

  // One Halley step on f(x) = N(x) - q. It is skipped if the correction is not finite, which can
  // happen only for q so small that exp(x^2 / 2) overflows; the rational estimate is then kept.
  const double error = normal_cdf(x) - q;
  const double u = error * std::sqrt(2.0 * std::numbers::pi) * std::exp(0.5 * x * x);
  if (std::isfinite(u)) {
    x -= u / (1.0 + 0.5 * x * u);
  }
  return x;
}

}  // namespace

double normal_pdf(double x) noexcept {
  return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi);
}

double normal_cdf(double x) noexcept {
  return 0.5 * std::erfc(-x / std::numbers::sqrt2);
}

Result<double> normal_quantile(double p) {
  if (std::isnan(p) || p <= 0.0 || p >= 1.0) {
    return Error{ErrorCode::invalid_argument,
                 "normal_quantile: probability must be strictly between 0 and 1"};
  }
  // 1 - p is exact for p in [0.5, 1), so the upper tail loses nothing by symmetry.
  return p <= 0.5 ? lower_tail_quantile(p) : -lower_tail_quantile(1.0 - p);
}

}  // namespace fxval::numerics
