// V-03: the Heston semi-analytic price must reduce to Garman-Kohlhagen when the volatility of
// variance goes to zero (docs/test_catalogue.md).
//
// With xi = 0 the variance is deterministic, v(t) = theta + (v0 - theta) e^{-kappa t}, so the spot
// is lognormal with total variance W = integral of v, and the price is Garman-Kohlhagen with
// sigma^2 T = W. This is exact. For small xi > 0 the difference is a perturbation whose size is
// known from theory:
//   rho != 0  the correlation term is first order: |Heston - GK| ~ c * xi
//   rho == 0  the first-order term vanishes:       |Heston - GK| ~ c * xi^2
// so the test checks the ORDER of convergence, not just that the difference is small. A wrong
// characteristic function would typically break the order.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "fxval/models/heston.hpp"
#include "support/heston_test_support.hpp"

namespace {

using fxval::models::expected_integrated_variance;
using fxval::pricing::OptionType;
using fxval::testing::gk_call;
using fxval::testing::heston_value;
using fxval::testing::make_params;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

struct Scenario {
  double v0, kappa, theta, tau;
};

double gap(const Scenario& s, double xi, double rho, double strike) {
  const double spot = 1.1, rd = 0.03, rf = 0.01;
  const auto params = make_params(s.v0, s.kappa, s.theta, xi, rho);
  const double sigma = std::sqrt(expected_integrated_variance(params, s.tau) / s.tau);
  return std::abs(heston_value(OptionType::call, spot, strike, rd, rf, s.tau, params) -
                  gk_call(spot, strike, rd, rf, s.tau, sigma));
}

TEST(V03_HestonVsGarmanKohlhagen, ExactAtZeroVolOfVariance) {
  double worst = 0.0;
  int checked = 0;
  for (const Scenario s : {Scenario{0.04, 2.0, 0.05, 1.0},
                           Scenario{0.09, 0.5, 0.02, 5.0},
                           Scenario{0.01, 5.0, 0.16, 0.25},
                           Scenario{0.04, 1.0, 0.04, 30.0}}) {
    for (double rho : {-0.9, 0.0, 0.9}) {
      for (double strike : {0.7, 0.95, 1.1, 1.25, 2.0}) {
        const double g = gap(s, 0.0, rho, strike);
        worst = std::max(worst, g);
        ASSERT_LT(g, 1e-12) << "tau=" << s.tau << " rho=" << rho << " strike=" << strike;
        ++checked;
      }
    }
  }
  RecordProperty("cases", checked);
  RecordProperty("worst_abs_error", sci(worst));
}

TEST(V03_HestonVsGarmanKohlhagen, ConvergesToGarmanKohlhagenAtTheTheoreticalOrder) {
  const Scenario s{0.04, 2.0, 0.05, 1.0};
  double worst_at_1e7 = 0.0;
  for (double rho : {-0.7, 0.6}) {
    for (double strike : {0.9, 1.1, 1.3}) {
      // First order: each tenfold reduction of xi reduces the gap about tenfold. (Below xi = 1e-3
      // the second-order term is negligible; at xi = 1e-2 it is not near the money.)
      for (double xi : {1e-3, 1e-4, 1e-5}) {
        const double ratio = gap(s, xi, rho, strike) / gap(s, xi / 10.0, rho, strike);
        EXPECT_GT(ratio, 8.0) << "rho=" << rho << " k=" << strike << " xi=" << xi;
        EXPECT_LT(ratio, 12.0) << "rho=" << rho << " k=" << strike << " xi=" << xi;
      }
      worst_at_1e7 = std::max(worst_at_1e7, gap(s, 1e-7, rho, strike));
    }
  }
  // Second order at rho = 0: each tenfold reduction reduces the gap about a hundredfold.
  for (double strike : {0.9, 1.1, 1.3}) {
    for (double xi : {1e-1, 1e-2}) {
      const double ratio = gap(s, xi, 0.0, strike) / gap(s, xi / 10.0, 0.0, strike);
      EXPECT_GT(ratio, 80.0) << "k=" << strike << " xi=" << xi;
      EXPECT_LT(ratio, 125.0) << "k=" << strike << " xi=" << xi;
    }
  }
  // The catalogue criterion: prices agree within 1e-6 as xi goes to 0.
  EXPECT_LT(worst_at_1e7, 1e-6);
  RecordProperty("worst_gap_at_xi_1e-7", sci(worst_at_1e7));
}

}  // namespace
