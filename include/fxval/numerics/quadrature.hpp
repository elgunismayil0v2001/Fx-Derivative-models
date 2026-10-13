#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// Adaptive Gauss-Kronrod (7, 15) quadrature, in the style of QUADPACK qag: start from 8 equal
// panels and bisect the panel with the largest error estimate until the total error is within
// tolerance. Header-only because the integrand is a template argument.
//
// Used by the Heston pricer. Tested on its own in tests/unit/test_quadrature.cpp.

namespace fxval::numerics {
namespace detail {

inline constexpr std::array<double, 8> kGk15Nodes = {0.991455371120812639206854697526329,
                                                     0.949107912342758524526189684047851,
                                                     0.864864423359769072789712788640926,
                                                     0.741531185599394439863864773280788,
                                                     0.586087235467691130294144838258730,
                                                     0.405845151377397166906606412076961,
                                                     0.207784955007898467600689403773245,
                                                     0.0};
inline constexpr std::array<double, 8> kGk15Weights = {0.022935322010529224963732008058970,
                                                       0.063092092629978553290700663189204,
                                                       0.104790010322250183839876322541518,
                                                       0.140653259715525918745189590510238,
                                                       0.169004726639267902826583426598550,
                                                       0.190350578064785409913256402421014,
                                                       0.204432940075298892414161999234649,
                                                       0.209482141084727828012999174891714};
// Weights of the embedded 7-point Gauss rule at nodes 1, 3, 5, 7 of the list above.
inline constexpr std::array<double, 4> kGauss7Weights = {0.129484966168869693270611432679082,
                                                         0.279705391489276667901467771423780,
                                                         0.381830050505118944950369775488975,
                                                         0.417959183673469387755102040816327};

struct Panel {
  double a;
  double b;
  double value;
  double error;      // estimate of the truncation error of this panel
  double abs_value;  // integral of |f| over the panel, which sets the rounding level
};

template <class F>
Panel gk15(const F& f, double a, double b) {
  const double centre = 0.5 * (a + b);
  const double half = 0.5 * (b - a);
  std::array<double, 15> fv{};
  fv[7] = f(centre);
  double kronrod = fv[7] * kGk15Weights[7];
  double gauss = fv[7] * kGauss7Weights[3];
  double abs_sum = std::abs(kronrod);
  for (std::size_t j = 0; j < 7; ++j) {
    const double dx = half * kGk15Nodes[j];
    fv[j] = f(centre - dx);
    fv[14 - j] = f(centre + dx);
    const double sum = fv[j] + fv[14 - j];
    kronrod += kGk15Weights[j] * sum;
    abs_sum += kGk15Weights[j] * (std::abs(fv[j]) + std::abs(fv[14 - j]));
    if (j % 2 == 1) {
      gauss += kGauss7Weights[j / 2] * sum;
    }
  }
  // Mean absolute deviation of the integrand on the panel, used by QUADPACK to scale the error.
  const double mean = kronrod * 0.5;  // integral / (b - a), since the weights sum to 2
  double deviation = kGk15Weights[7] * std::abs(fv[7] - mean);
  for (std::size_t j = 0; j < 7; ++j) {
    deviation += kGk15Weights[j] * (std::abs(fv[j] - mean) + std::abs(fv[14 - j] - mean));
  }
  const double value = kronrod * half;
  double error = std::abs((kronrod - gauss) * half);
  const double scaled_deviation = deviation * std::abs(half);
  if (scaled_deviation != 0.0 && error != 0.0) {
    error = scaled_deviation * std::min(1.0, std::pow(200.0 * error / scaled_deviation, 1.5));
  }
  return Panel{a, b, value, error, abs_sum * std::abs(half)};
}

}  // namespace detail

// value:     the integral estimate
// error:     the estimated truncation error (the rounding level is handled in the stop rule)
// converged: true when error <= max(abs_tol, rel_tol * |value|, rounding level)
struct QuadratureResult {
  double value;
  double error;
  bool converged;
};

template <class F>
[[nodiscard]] QuadratureResult adaptive_gauss_kronrod(
    const F& f, double a, double b, double abs_tol, double rel_tol, std::size_t max_panels) {
  using detail::gk15;
  using detail::Panel;
  std::vector<Panel> panels;
  // Start from 8 equal panels so that a narrow feature cannot hide inside one initial panel.
  constexpr int kInitial = 8;
  for (int i = 0; i < kInitial; ++i) {
    const double lo = a + (b - a) * i / kInitial;
    const double hi = a + (b - a) * (i + 1) / kInitial;
    panels.push_back(gk15(f, lo, hi));
  }
  for (;;) {
    double total = 0.0;
    double err = 0.0;
    double abs_total = 0.0;
    for (const auto& p : panels) {
      total += p.value;
      err += p.error;
      abs_total += p.abs_value;
    }
    // A sum of floating-point terms cannot be more accurate than about eps times the sum of their
    // magnitudes, so a tolerance below that level is relaxed to it instead of never being met.
    constexpr double kRounding = 10.0 * std::numeric_limits<double>::epsilon();
    if (err <= std::max({abs_tol, rel_tol * std::abs(total), kRounding * abs_total})) {
      return QuadratureResult{total, err, true};
    }
    if (panels.size() >= max_panels) {
      return QuadratureResult{total, err, false};
    }
    auto worst = std::max_element(panels.begin(), panels.end(), [](const Panel& x, const Panel& y) {
      return x.error < y.error;
    });
    const Panel p = *worst;
    const double mid = 0.5 * (p.a + p.b);
    if (mid <= p.a || mid >= p.b) {  // interval can no longer be split in double precision
      return QuadratureResult{total, err, false};
    }
    *worst = gk15(f, p.a, mid);
    panels.push_back(gk15(f, mid, p.b));
  }
}

}  // namespace fxval::numerics
