// V-01: put-call parity on a grid of 8000 markets (see docs/test_catalogue.md).
// Evidence about the model implementation: parity is an exact no-arbitrage identity.

#include <gtest/gtest.h>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "support/gk_test_support.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

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
// V-01: put-call parity.  Call - Put = S e^{-r_f T} - K e^{-r_d T}
// Error is measured relative to the larger of the two discounted amounts, so the tolerance means
// the same thing for a spot near 1 and a spot near 100.
// ---------------------------------------------------------------------------------------------
TEST(V01_PutCallParity, HoldsOnAGridOfMarkets) {
  int checked = 0;
  double worst = 0.0;
  for (double s : {0.8, 1.0, 1.1, 4.3, 100.0}) {
    for (double moneyness : {0.6, 0.9, 1.0, 1.1, 1.8}) {
      for (double rd : {-0.01, 0.0, 0.03, 0.08}) {
        for (double rf : {-0.01, 0.0, 0.02, 0.08}) {
          for (double sigma : {0.01, 0.08, 0.25, 1.0}) {
            for (double t : {1.0 / 365.0, 0.25, 1.0, 5.0, 30.0}) {
              const double k = s * moneyness;
              const double call = gk_value(OptionType::call, s, k, rd, rf, sigma, t);
              const double put = gk_value(OptionType::put, s, k, rd, rf, sigma, t);
              const double lhs = call - put;
              const double rhs = s * std::exp(-rf * t) - k * std::exp(-rd * t);
              const double scale = std::max(s * std::exp(-rf * t), k * std::exp(-rd * t));
              const double err = std::abs(lhs - rhs) / scale;
              worst = std::max(worst, err);
              ASSERT_LT(err, fxval::tol::put_call_parity)
                  << "s=" << s << " k=" << k << " rd=" << rd << " rf=" << rf << " sigma=" << sigma
                  << " t=" << t;
              ++checked;
            }
          }
        }
      }
    }
  }
  RecordProperty("cases", checked);
  RecordProperty("worst_relative_error", sci(worst));
  EXPECT_EQ(checked, 8000);  // 5 spots x 5 strikes x 4 r_d x 4 r_f x 4 sigmas x 5 expiries
}

}  // namespace
