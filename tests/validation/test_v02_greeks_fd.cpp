#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace {

using fxval::Rate;
using fxval::Spot;
using fxval::Strike;
using fxval::Volatility;
using fxval::Years;
using fxval::pricing::garman_kohlhagen_greeks;
using fxval::pricing::garman_kohlhagen_price;
using fxval::pricing::OptionType;
using fxval::pricing::VanillaFx;

VanillaFx market(double spot,
                 double strike,
                 double domestic,
                 double foreign,
                 double years,
                 double vol) {
  return VanillaFx{
      Spot::from_value(spot).value(),
      Strike::from_value(strike).value(),
      Rate::from_decimal(domestic).value(),
      Rate::from_decimal(foreign).value(),
      Years::from_years(years).value(),
      Volatility::from_decimal(vol).value(),
  };
}

double price_at(double spot,
                double strike,
                double domestic,
                double foreign,
                double years,
                double vol,
                OptionType type) {
  const auto p = garman_kohlhagen_price(market(spot, strike, domestic, foreign, years, vol), type);
  EXPECT_TRUE(p.has_value());
  return p.value();
}

double relative_gap(double analytic, double finite_difference) {
  const double scale = std::max({std::abs(analytic), std::abs(finite_difference), 1e-8});
  return std::abs(analytic - finite_difference) / scale;
}

// One step of Richardson extrapolation on a central-difference stencil.
double richardson(double coarse, double fine) { return (4.0 * fine - coarse) / 3.0; }

struct Case {
  double spot;
  double strike;
  double domestic;
  double foreign;
  double years;
  double vol;
  OptionType type;
};

TEST(V02, GreeksMatchCentralFiniteDifferences) {
  const std::vector<Case> cases = {
      {1.10, 1.10, 0.03, 0.01, 1.00, 0.10, OptionType::call},
      {1.10, 1.10, 0.03, 0.01, 1.00, 0.10, OptionType::put},
      {4.35, 4.10, 0.05, 0.03, 0.75, 0.12, OptionType::call},
      {4.35, 4.60, 0.05, 0.03, 0.75, 0.12, OptionType::put},
      {0.92, 0.95, 0.01, 0.04, 2.00, 0.18, OptionType::call},
  };

  for (const auto& c : cases) {
    const auto m = market(c.spot, c.strike, c.domestic, c.foreign, c.years, c.vol);
    const auto g = garman_kohlhagen_greeks(m, c.type);
    ASSERT_TRUE(g.has_value());
    const auto& analytic = g.value();

    auto pr = [&](double spot, double domestic, double foreign, double years, double vol) {
      return price_at(spot, c.strike, domestic, foreign, years, vol, c.type);
    };

    const double mid = pr(c.spot, c.domestic, c.foreign, c.years, c.vol);
    const double hs = 1e-4 * c.spot;
    const double hv = 1e-4;
    const double ht = 1e-5;
    const double hr = 1e-6;

    auto delta_fd = [&](double h) {
      return (pr(c.spot + h, c.domestic, c.foreign, c.years, c.vol) -
              pr(c.spot - h, c.domestic, c.foreign, c.years, c.vol)) /
             (2.0 * h);
    };
    auto gamma_fd = [&](double h) {
      return (pr(c.spot + h, c.domestic, c.foreign, c.years, c.vol) - 2.0 * mid +
              pr(c.spot - h, c.domestic, c.foreign, c.years, c.vol)) /
             (h * h);
    };
    auto vega_fd = [&](double h) {
      return (pr(c.spot, c.domestic, c.foreign, c.years, c.vol + h) -
              pr(c.spot, c.domestic, c.foreign, c.years, c.vol - h)) /
             (2.0 * h);
    };
    auto volga_fd = [&](double h) {
      return (pr(c.spot, c.domestic, c.foreign, c.years, c.vol + h) - 2.0 * mid +
              pr(c.spot, c.domestic, c.foreign, c.years, c.vol - h)) /
             (h * h);
    };
    auto vanna_fd = [&](double h_s, double h_v) {
      return (pr(c.spot + h_s, c.domestic, c.foreign, c.years, c.vol + h_v) -
              pr(c.spot + h_s, c.domestic, c.foreign, c.years, c.vol - h_v) -
              pr(c.spot - h_s, c.domestic, c.foreign, c.years, c.vol + h_v) +
              pr(c.spot - h_s, c.domestic, c.foreign, c.years, c.vol - h_v)) /
             (4.0 * h_s * h_v);
    };

    const double fd_delta = richardson(delta_fd(hs), delta_fd(hs / 2.0));
    const double fd_gamma = richardson(gamma_fd(hs), gamma_fd(hs / 2.0));
    const double fd_vega = richardson(vega_fd(hv), vega_fd(hv / 2.0));
    const double fd_volga = richardson(volga_fd(hv), volga_fd(hv / 2.0));
    const double fd_vanna = richardson(vanna_fd(hs, hv), vanna_fd(hs / 2.0, hv / 2.0));

    const double fd_theta =
        (pr(c.spot, c.domestic, c.foreign, c.years - ht, c.vol) -
         pr(c.spot, c.domestic, c.foreign, c.years + ht, c.vol)) /
        (2.0 * ht);
    const double fd_rho_d =
        (pr(c.spot, c.domestic + hr, c.foreign, c.years, c.vol) -
         pr(c.spot, c.domestic - hr, c.foreign, c.years, c.vol)) /
        (2.0 * hr);
    const double fd_rho_f =
        (pr(c.spot, c.domestic, c.foreign + hr, c.years, c.vol) -
         pr(c.spot, c.domestic, c.foreign - hr, c.years, c.vol)) /
        (2.0 * hr);
    const double df_f = std::exp(-c.foreign * c.years);
    const double fd_fwd_delta = fd_delta / df_f;

    auto expect_close = [&](const char* name, double a, double fd) {
      EXPECT_LT(relative_gap(a, fd), fxval::tol::greeks_relative_fd)
          << name << " analytic=" << a << " fd=" << fd << " type="
          << (c.type == OptionType::call ? "call" : "put") << " spot=" << c.spot;
    };

    expect_close("spot_delta", analytic.spot_delta, fd_delta);
    expect_close("forward_delta", analytic.forward_delta, fd_fwd_delta);
    expect_close("gamma", analytic.gamma, fd_gamma);
    expect_close("vega", analytic.vega, fd_vega);
    expect_close("vanna", analytic.vanna, fd_vanna);
    expect_close("volga", analytic.volga, fd_volga);
    expect_close("theta", analytic.theta, fd_theta);
    expect_close("rho_domestic", analytic.rho_domestic, fd_rho_d);
    expect_close("rho_foreign", analytic.rho_foreign, fd_rho_f);
  }
}

}  // namespace
