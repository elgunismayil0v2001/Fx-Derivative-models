// V-02: analytic Greeks against finite differences of the price (see docs/test_catalogue.md).
// Volga is differenced from the analytic vega: second differences of the price are limited by
// rounding noise for short expiries.

#include <gtest/gtest.h>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "support/gk_test_support.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using fxval::pricing::GarmanKohlhagenGreeks;
using fxval::pricing::OptionType;
using fxval::testing::gk_all;
using fxval::testing::gk_value;

// Scientific notation for the recorded worst errors (std::to_string would print 0.000000).
std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

// ---------------------------------------------------------------------------------------------
// V-02: Greeks against finite differences of the price.
//
// Stencils are fourth order (error ~ h^4), so a step of a few percent of the natural scale of
// each variable gives truncation error far below the tolerance without amplifying rounding error.
//   f'(x)  ~ [-f(x+2h) + 8 f(x+h) - 8 f(x-h) + f(x-2h)] / (12 h)
//   f''(x) ~ [-f(x+2h) + 16 f(x+h) - 30 f(x) + 16 f(x-h) - f(x-2h)] / (12 h^2)
// Vanna is the derivative in sigma of the spot derivative (the two stencils nested).
// ---------------------------------------------------------------------------------------------
using Fn = std::function<double(double)>;

double d1_stencil(const Fn& f, double x, double h) {
  return (-f(x + 2 * h) + 8 * f(x + h) - 8 * f(x - h) + f(x - 2 * h)) / (12 * h);
}

double d2_stencil(const Fn& f, double x, double h) {
  return (-f(x + 2 * h) + 16 * f(x + h) - 30 * f(x) + 16 * f(x - h) - f(x - 2 * h)) / (12 * h * h);
}

struct Point {
  double s, k, rd, rf, sigma, t;
};

// Strike placed `m` total standard deviations from the forward, so every point is in the region
// where the price is smooth on the scale of the steps (no case is so deep out of the money that a
// small step changes the price by orders of magnitude).
Point make_point(double s, double rd, double rf, double sigma, double t, double m) {
  const double forward = s * std::exp((rd - rf) * t);
  return {s, forward * std::exp(-m * sigma * std::sqrt(t)), rd, rf, sigma, t};
}

std::vector<Point> fd_points() {
  std::vector<Point> points;
  // (sigma, t): total standard deviation from about 1.4% to 57%.
  const double sigma_t[][2] = {{0.10, 0.02}, {0.08, 0.25}, {0.15, 1.0}, {0.40, 2.0}};
  const double rates[][2] = {{0.03, 0.01}, {-0.005, 0.02}, {0.06, 0.06}};
  for (const auto& st : sigma_t) {
    for (const auto& r : rates) {
      for (double m : {-1.5, -0.5, 0.0, 0.5, 1.5}) {
        points.push_back(make_point(1.10, r[0], r[1], st[0], st[1], m));
      }
    }
  }
  return points;
}

std::map<std::string, double> g_worst_by_greek;    // largest error seen per Greek, for the report
std::map<std::string, std::string> g_worst_where;  // and the point where it happened

// |analytic - finite difference| <= tol * (|analytic| + floor). The floor stops a Greek that
// passes through zero (vanna and volga change sign) from turning a rounding-level difference
// into a huge relative error. It is 1e-3 of the Greek's natural size, stated per Greek below.
void expect_close(double analytic,
                  double fd,
                  double floor,
                  const char* name,
                  const Point& p,
                  OptionType type,
                  double* worst) {
  const double err = std::abs(analytic - fd) / (std::abs(analytic) + floor);
  *worst = std::max(*worst, err);
  double& by_greek = g_worst_by_greek[name];
  if (err > by_greek) {
    std::ostringstream where;
    where << (type == OptionType::call ? "call" : "put") << " k=" << p.k << " rd=" << p.rd
          << " rf=" << p.rf << " sigma=" << p.sigma << " t=" << p.t << " analytic=" << analytic
          << " fd=" << fd;
    g_worst_where[name] = where.str();
    by_greek = err;
  }
  EXPECT_LT(err, fxval::tol::greeks_relative_fd)
      << name << (type == OptionType::call ? " call" : " put") << " s=" << p.s << " k=" << p.k
      << " rd=" << p.rd << " rf=" << p.rf << " sigma=" << p.sigma << " t=" << p.t
      << " analytic=" << analytic << " fd=" << fd;
}

TEST(V02_GreeksVsFiniteDifferences, AllGreeksAgreeWithDifferencesOfThePrice) {
  double worst = 0.0;
  int checked = 0;
  for (const auto& p : fd_points()) {
    for (auto type : {OptionType::call, OptionType::put}) {
      const GarmanKohlhagenGreeks g = gk_all(type, p.s, p.k, p.rd, p.rf, p.sigma, p.t);
      const double sd = p.sigma * std::sqrt(p.t);

      const double h_s = 0.02 * p.s * sd;
      const double h_sigma = 0.01 * p.sigma;
      const double h_t = 0.02 * p.t;
      const double h_r = 1e-3;

      auto price_s = [&](double s) {
        return gk_value(type, s, p.k, p.rd, p.rf, p.sigma, p.t);
      };
      auto price_sigma = [&](double v) {
        return gk_value(type, p.s, p.k, p.rd, p.rf, v, p.t);
      };
      auto price_t = [&](double t) {
        return gk_value(type, p.s, p.k, p.rd, p.rf, p.sigma, t);
      };
      auto price_rd = [&](double r) {
        return gk_value(type, p.s, p.k, r, p.rf, p.sigma, p.t);
      };
      auto price_rf = [&](double r) {
        return gk_value(type, p.s, p.k, p.rd, r, p.sigma, p.t);
      };
      auto delta_of_sigma = [&](double v) {
        return d1_stencil(
            [&](double s) { return gk_value(type, s, p.k, p.rd, p.rf, v, p.t); }, p.s, h_s);
      };

      const double vega_scale = g.vega;
      expect_close(g.spot_delta,
                   d1_stencil(price_s, p.s, h_s),
                   1e-3 * g.spot_delta,
                   "delta",
                   p,
                   type,
                   &worst);
      expect_close(
          g.gamma, d2_stencil(price_s, p.s, h_s), 1e-3 * g.gamma, "gamma", p, type, &worst);
      expect_close(g.vega,
                   d1_stencil(price_sigma, p.sigma, h_sigma),
                   1e-3 * vega_scale,
                   "vega",
                   p,
                   type,
                   &worst);
      // Volga is d(vega)/d(sigma), so it is checked as a first difference of the analytic vega
      // (which the line above has already checked against the price). A second difference of the
      // price would be limited by rounding noise (eps / h^2): for short expiries the price is a
      // difference of two similar numbers, and that noise reaches the 1e-6 level.
      auto vega_of_sigma = [&](double v) {
        return gk_all(type, p.s, p.k, p.rd, p.rf, v, p.t).vega;
      };
      expect_close(g.volga,
                   d1_stencil(vega_of_sigma, p.sigma, h_sigma),
                   1e-3 * vega_scale / p.sigma,
                   "volga",
                   p,
                   type,
                   &worst);
      expect_close(g.vanna,
                   d1_stencil(delta_of_sigma, p.sigma, h_sigma),
                   1e-3 * vega_scale / (p.s * sd),
                   "vanna",
                   p,
                   type,
                   &worst);
      // theta is dV/dt = -dV/dT
      expect_close(g.theta,
                   -d1_stencil(price_t, p.t, h_t),
                   1e-3 * vega_scale * p.sigma / std::sqrt(p.t),
                   "theta",
                   p,
                   type,
                   &worst);
      expect_close(g.rho_domestic,
                   d1_stencil(price_rd, p.rd, h_r),
                   1e-3 * p.k * p.t,
                   "rho_d",
                   p,
                   type,
                   &worst);
      expect_close(g.rho_foreign,
                   d1_stencil(price_rf, p.rf, h_r),
                   1e-3 * p.s * p.t,
                   "rho_f",
                   p,
                   type,
                   &worst);
      ++checked;
    }
  }
  RecordProperty("points", checked);
  RecordProperty("worst_relative_error", sci(worst));
  for (const auto& [name, err] : g_worst_by_greek) {
    RecordProperty("worst_" + name, sci(err));
    RecordProperty("where_" + name, g_worst_where[name]);
  }
  EXPECT_EQ(checked, 120);  // 4 (sigma, T) x 3 rate pairs x 5 moneyness x call and put
}

}  // namespace
