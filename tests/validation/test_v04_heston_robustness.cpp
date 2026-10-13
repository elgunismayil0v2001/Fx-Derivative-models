// V-04: robustness of the Heston semi-analytic pricer over long maturities and extreme parameters
// (docs/test_catalogue.md). Two kinds of failure are looked for:
//
//  1. Branch-cut errors. The original Heston (1993) characteristic function takes a complex
//     logarithm and a complex square root; for long maturities the logarithm jumps by 2 pi i
//     and the price becomes wrong, often without any NaN. Test A walks the characteristic
//     function along maturity and fails on any jump. It also runs the ORIGINAL formulation on a
//     known bad case and requires that the test DOES detect a jump there: a check that cannot
//     fail proves nothing.
//  2. Loss of accuracy or consistency in the price itself. Test B prices a grid of parameters
//     including Feller violations, very short and very long expiries, strong correlation and
//     large vol of vol, and checks properties every correct price has: finite, inside the
//     no-arbitrage bounds, decreasing and convex in the strike, and the bounds on the
//     characteristic function that follow from the martingale property. None of these uses a second
//     implementation of the model.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "fxval/models/heston.hpp"
#include "support/heston_test_support.hpp"

namespace {

using fxval::models::expected_integrated_variance;
using fxval::models::heston_price;
using fxval::models::HestonParams;
using fxval::models::log_forward_characteristic_function;
using fxval::pricing::OptionType;
using fxval::testing::make_heston_market;
using fxval::testing::make_params;
using Complex = std::complex<double>;

std::string describe(double v0, double kappa, double theta, double xi, double rho, double tau) {
  std::ostringstream out;
  out << "v0=" << v0 << " kappa=" << kappa << " theta=" << theta << " xi=" << xi << " rho=" << rho
      << " T=" << tau;
  return out.str();
}

// The characteristic function of the original Heston (1993) paper, with the "+d" root and
// e^{+d tau}. Kept here only to show that test A can detect a branch-cut jump.
Complex original_formulation(const HestonParams& p, double tau, Complex u) {
  const Complex iu(-u.imag(), u.real());
  const Complex h = iu + u * u;
  const Complex a = p.kappa() - p.rho() * p.xi() * iu;
  const Complex d = std::sqrt(a * a + p.xi() * p.xi() * h);
  const Complex g = (a + d) / (a - d);
  const Complex e = std::exp(d * tau);
  const double xi2 = p.xi() * p.xi();
  const Complex big_d = (a + d) / xi2 * (1.0 - e) / (1.0 - g * e);
  const Complex big_c =
      p.kappa() * p.theta() / xi2 * ((a + d) * tau - 2.0 * std::log((1.0 - g * e) / (1.0 - g)));
  return std::exp(big_c + big_d * p.v0());
}

// Largest relative change of phi between neighbouring maturities. A smooth function changes by a
// small fraction of itself over one step; a branch jump changes it by order one.
template <class Cf>
double largest_relative_step(const Cf& cf, double step, double max_tau) {
  double worst = 0.0;
  Complex previous = cf(step);
  for (double tau = 2.0 * step; tau <= max_tau; tau += step) {
    const Complex current = cf(tau);
    if (std::isfinite(previous.real()) && std::isfinite(previous.imag()) &&
        std::isfinite(current.real()) && std::isfinite(current.imag())) {
      const double scale = std::max(std::abs(previous), std::abs(current));
      if (scale > 0.0) {
        worst = std::max(worst, std::abs(current - previous) / scale);
      }
    }
    previous = current;
  }
  return worst;
}

constexpr double kJump = 0.25;  // smooth steps measured below are about 0.02; a jump is about 1

TEST(V04_HestonRobustness, CharacteristicFunctionHasNoBranchJumpsInMaturity) {
  double worst = 0.0;
  int checked = 0;
  for (double kappa : {0.05, 0.3, 3.0}) {
    for (double v0 : {0.04, 0.25}) {
      for (double xi : {0.5, 2.0, 4.0}) {
        for (double rho : {-0.9, 0.9}) {
          for (double s : {0.5, 6.0}) {  // u = s - i/2, the line used by the pricer
            const auto p = make_params(v0, kappa, 0.04, xi, rho);
            const double jump = largest_relative_step(
                [&](double tau) {
                  return log_forward_characteristic_function(p, tau, Complex(s, -0.5));
                },
                0.005,
                30.0);
            worst = std::max(worst, jump);
            EXPECT_LT(jump, kJump) << describe(v0, kappa, 0.04, xi, rho, 30.0) << " s=" << s;
            ++checked;
          }
        }
      }
    }
  }
  RecordProperty("cases", checked);
  RecordProperty("worst_relative_step", worst);
}

TEST(V04_HestonRobustness, TheOriginalFormulationDoesJumpSoTheCheckCanFail) {
  // kappa = 0.3, theta = v0 = 0.04, xi = 0.5, rho = -0.9: the original formulation is
  // discontinuous around T of 16 years, the stable one is not.
  const auto p = make_params(0.04, 0.3, 0.04, 0.5, -0.9);
  const Complex u(0.5, -0.5);
  const double original = largest_relative_step(
      [&](double tau) { return original_formulation(p, tau, u); }, 0.005, 30.0);
  const double stable = largest_relative_step(
      [&](double tau) { return log_forward_characteristic_function(p, tau, u); }, 0.005, 30.0);
  EXPECT_GT(original, kJump);
  EXPECT_LT(stable, kJump);
  RecordProperty("original_formulation_worst_step", original);
  RecordProperty("stable_formulation_worst_step", stable);
}

TEST(V04_HestonRobustness, PricesAreFiniteBoundedMonotoneAndConvexOnAnExtremeGrid) {
  int param_sets = 0;
  int prices = 0;
  int feller_violations = 0;
  double worst_bound = -1.0;  // largest excess of |phi| over its bound; negative = inside
  const double spot = 1.1, rd = 0.03, rf = 0.01;
  for (double kappa : {0.05, 1.0, 10.0}) {
    for (double theta : {0.01, 0.09}) {
      for (double v0 : {0.01, 0.25}) {
        for (double xi : {0.05, 0.5, 2.0, 5.0}) {
          for (double rho : {-0.95, 0.0, 0.95}) {
            for (double tau : {1.0 / 52.0, 1.0, 10.0, 30.0}) {
              const auto p = make_params(v0, kappa, theta, xi, rho);
              const std::string where = describe(v0, kappa, theta, xi, rho, tau);
              ++param_sets;
              feller_violations += p.feller_satisfied() ? 0 : 1;

              // A characteristic function is bounded by its value at the "tilted" origin:
              // |phi(s - i)| <= phi(-i) = E[S_T / F] = 1 (martingale property) and
              // |phi(s - i/2)| <= phi(-i/2) = E[sqrt(S_T / F)] <= 1. Checked on both lines,
              // including points close to the removable singularity at u = -i.
              for (double s : {1e-3, 0.1, 1.0, 10.0}) {
                const double on_unit =
                    std::abs(log_forward_characteristic_function(p, tau, Complex(s, -1.0)));
                const double on_half =
                    std::abs(log_forward_characteristic_function(p, tau, Complex(s, -0.5)));
                worst_bound = std::max({worst_bound, on_unit - 1.0, on_half - 1.0});
                EXPECT_LE(on_unit, 1.0 + 1e-10) << where << " s=" << s;
                EXPECT_LE(on_half, 1.0 + 1e-10) << where << " s=" << s;
              }

              const double forward = spot * std::exp((rd - rf) * tau);
              const double df_d = std::exp(-rd * tau);
              const double total_sd = std::sqrt(expected_integrated_variance(p, tau));
              std::vector<double> strikes, calls;
              for (double m : {-3.0, -1.0, 0.0, 1.0, 3.0}) {
                const double strike = forward * std::exp(m * total_sd);
                const auto call = heston_price(
                    make_heston_market(spot, strike, rd, rf, tau), p, OptionType::call);
                ASSERT_TRUE(call.has_value())
                    << where << " m=" << m << ": " << call.error().message;
                const double c = call.value();
                ++prices;
                ASSERT_TRUE(std::isfinite(c)) << where;
                const double lower = std::max(0.0, df_d * (forward - strike));
                EXPECT_GE(c, lower - 1e-12 * df_d * forward) << where << " m=" << m;
                EXPECT_LE(c, df_d * forward * (1.0 + 1e-12)) << where << " m=" << m;
                strikes.push_back(strike);
                calls.push_back(c);
              }
              const double slack = 1e-11 * df_d * forward;  // quadrature tolerance is 1e-14 F
              double previous_slope = -1e300;
              for (std::size_t i = 0; i + 1 < strikes.size(); ++i) {
                const double dk = strikes[i + 1] - strikes[i];
                const double slope = (calls[i + 1] - calls[i]) / dk;
                EXPECT_LE(calls[i + 1], calls[i] + slack) << "not decreasing: " << where;
                EXPECT_GE(slope, previous_slope - slack / dk)
                    << "not convex: " << where << " i=" << i;
                previous_slope = slope;
              }
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(param_sets, 3 * 2 * 2 * 4 * 3 * 4);
  EXPECT_GT(feller_violations, 0);  // the grid must include Feller violations
  RecordProperty("parameter_sets", param_sets);
  RecordProperty("prices", prices);
  RecordProperty("feller_violations", feller_violations);
  RecordProperty("worst_bound_excess", worst_bound);
}

}  // namespace
