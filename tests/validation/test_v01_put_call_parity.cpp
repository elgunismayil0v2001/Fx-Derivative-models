#include <gtest/gtest.h>

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

struct Case {
  double spot;
  double strike;
  double domestic;
  double foreign;
  double years;
  double vol;
};

TEST(V01, PutCallParityGarmanKohlhagen) {
  const std::vector<Case> cases = {
      {1.10, 1.10, 0.03, 0.01, 1.00, 0.10},
      {4.35, 4.00, 0.05, 0.03, 0.25, 0.12},
      {4.35, 4.80, 0.05, 0.03, 2.00, 0.20},
      {0.85, 0.90, 0.00, 0.02, 0.50, 0.08},
      {1.25, 1.25, -0.005, 0.01, 5.00, 0.15},
      {1.00, 1.20, 0.04, 0.04, 0.01, 0.30},
      {1.10, 1.10, 0.03, 0.01, 0.00, 0.10},
      {1.10, 1.10, 0.03, 0.01, 1.00, 0.00},
  };

  for (const auto& c : cases) {
    const auto m = market(c.spot, c.strike, c.domestic, c.foreign, c.years, c.vol);
    const auto call = garman_kohlhagen_price(m, OptionType::call);
    const auto put = garman_kohlhagen_price(m, OptionType::put);
    ASSERT_TRUE(call.has_value()) << "call failed for spot=" << c.spot;
    ASSERT_TRUE(put.has_value()) << "put failed for spot=" << c.spot;
    const double rhs = c.spot * std::exp(-c.foreign * c.years) -
                       c.strike * std::exp(-c.domestic * c.years);
    EXPECT_NEAR(call.value() - put.value(), rhs, fxval::tol::put_call_parity)
        << "spot=" << c.spot << " strike=" << c.strike << " T=" << c.years << " vol=" << c.vol;
  }
}

}  // namespace
