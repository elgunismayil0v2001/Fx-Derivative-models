// V-18: delta-normal against full revaluation for option portfolios (the gap in VaR, quantified).
//
// Spot 1.10, domestic 3%, foreign 1%, notional 1,000,000 per leg, volatility 8% per year, return
// volatility sigma_h = 0.08 sqrt(h / 252) for a horizon of h = 1 and h = 10 days, level 99%, the
// quantile-grid full revaluation with 100,000 points (deterministic). Six portfolios:
//   P1 long ATM straddle 3M          P2 short ATM straddle 3M        P3 short 1M strangle (+-3%)
//   P4 long risk reversal 3M         P5 short ATM straddle 3W        P6 long 1% OTM call 3W
// Reported: VaR by delta-normal, delta-gamma (Cornish-Fisher) and full revaluation, and the gap
// (method - full) / full.
// Asserted (decided before the run):
//   1. Convexity ordering, which holds scenario by scenario: for the long-gamma portfolio P1 and
//   the
//      long call P6, VaR(full) <= VaR(delta-normal) + 1e-6; for the short-gamma portfolios P2, P3,
//      P5, VaR(full) >= VaR(delta-normal) - 1e-6. (The full P&L lies above (below) the tangent for
//      every scenario, so the quantile does too.)
//   2. For the short straddles P2 and P5 at the 10-day horizon, delta-normal understates the full
//   VaR by
//      more than 30%.
//   3. For the four pure straddle and strangle portfolios (P1, P2, P3, P5) delta-gamma is closer to
//      full revaluation than delta-normal is, at both horizons.
//   4. CONTROL: the three methods agree (within 0.5% of the full VaR) for a pure spot position, so
//   the
//      gaps above are the options' and not the machinery's.
// Not asserted: the size of the gaps for P4 and P6 and the sign of the gap of delta-gamma.
// DISCLOSURE: P5 and P6 were first written with a one-week expiry, which is shorter than the
// 10-day horizon (the option expires inside it and the revaluation is meaningless). Changed to
// three weeks before the results were accepted; the assertions were not changed.

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <iostream>

#include "fxval/risk/portfolio.hpp"
#include "support/risk_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::must;
using pricing::OptionType;

risk::OptionLeg leg(OptionType type, double strike, double years, double qty) {
  return risk::OptionLeg{type,
                         must(Strike::from_value(strike)),
                         must(Years::from_years(years)),
                         must(Volatility::from_decimal(0.08)),
                         qty};
}

struct Named {
  const char* name;
  risk::FxPortfolio pf;
};

std::vector<Named> portfolios() {
  const auto make = [](std::vector<risk::OptionLeg> legs) {
    auto pf = fxval::testing::spot_portfolio(0.0);
    pf.options = std::move(legs);
    return pf;
  };
  constexpr double n = 1e6;
  return {{"P1 long ATM straddle 3M",
           make({leg(OptionType::call, 1.10, 0.25, n), leg(OptionType::put, 1.10, 0.25, n)})},
          {"P2 short ATM straddle 3M",
           make({leg(OptionType::call, 1.10, 0.25, -n), leg(OptionType::put, 1.10, 0.25, -n)})},
          {"P3 short strangle 1M",
           make({leg(OptionType::call, 1.133, 1.0 / 12.0, -n),
                 leg(OptionType::put, 1.067, 1.0 / 12.0, -n)})},
          {"P4 long risk reversal 3M",
           make({leg(OptionType::call, 1.14, 0.25, n), leg(OptionType::put, 1.06, 0.25, -n)})},
          {"P5 short ATM straddle 3W",
           make({leg(OptionType::call, 1.10, 21.0 / 365.0, -n),
                 leg(OptionType::put, 1.10, 21.0 / 365.0, -n)})},
          {"P6 long 1% OTM call 3W", make({leg(OptionType::call, 1.111, 21.0 / 365.0, n)})}};
}

TEST(V18_DeltaNormalVsRevaluation, GapTableAndStructuralAssertions) {
  constexpr double level = 0.99;
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "\nV-18 99% VaR, currency units per 1,000,000 notional per leg\n"
            << std::left << std::setw(26) << "portfolio" << std::right << std::setw(5) << "days"
            << std::setw(12) << "delta-norm" << std::setw(12) << "delta-gam" << std::setw(12)
            << "full" << std::setw(10) << "DN gap%" << std::setw(10) << "DG gap%" << "\n";
  for (const Named& item : portfolios()) {
    for (double days : {1.0, 10.0}) {
      const double sigma = 0.08 * std::sqrt(days / 252.0);
      const auto dn = must(risk::delta_normal_var_es(item.pf, sigma, level));
      const auto dg = must(risk::delta_gamma_var_es(item.pf, sigma, level));
      const auto full = must(risk::full_revaluation_normal_var_es(item.pf, sigma, level));
      const double gap_dn = 100.0 * (dn.var - full.var) / full.var;
      const double gap_dg = 100.0 * (dg.var - full.var) / full.var;
      std::cout << std::left << std::setw(26) << item.name << std::right << std::setw(5) << days
                << std::setw(12) << dn.var << std::setw(12) << dg.var << std::setw(12) << full.var
                << std::setw(10) << gap_dn << std::setw(10) << gap_dg << "\n";
      const std::string name = item.name;
      const bool long_gamma = name.rfind("P1", 0) == 0 || name.rfind("P6", 0) == 0;
      const bool short_gamma =
          name.rfind("P2", 0) == 0 || name.rfind("P3", 0) == 0 || name.rfind("P5", 0) == 0;
      if (long_gamma) {
        EXPECT_LE(full.var, dn.var + 1e-6) << name << " " << days;
      }
      if (short_gamma) {
        EXPECT_GE(full.var, dn.var - 1e-6) << name << " " << days;
      }
      if ((name.rfind("P2", 0) == 0 || name.rfind("P5", 0) == 0) && days == 10.0) {
        EXPECT_LT(dn.var, 0.7 * full.var) << name;
      }
      const bool pure = long_gamma ? name.rfind("P1", 0) == 0 : short_gamma;
      if (pure) {
        EXPECT_LT(std::abs(dg.var - full.var), std::abs(dn.var - full.var)) << name << " " << days;
      }
    }
  }
}

TEST(V18_DeltaNormalVsRevaluation, ControlPureSpotPositionAgreesAcrossMethods) {
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const double sigma = 0.08 * std::sqrt(10.0 / 252.0);
  const auto dn = must(risk::delta_normal_var_es(pf, sigma, 0.99));
  const auto dg = must(risk::delta_gamma_var_es(pf, sigma, 0.99));
  const auto full = must(risk::full_revaluation_normal_var_es(pf, sigma, 0.99));
  EXPECT_LT(std::abs(dn.var - full.var), 0.005 * full.var);
  EXPECT_LT(std::abs(dg.var - full.var), 0.005 * full.var);
}

}  // namespace
