// V-09: delta quote -> strike -> quote round trip.
//
// Criterion (fixed before results were seen): every quote recovered to below tol::quote_roundtrip
// (1e-8), where "recovered" means:
//   (a) the Garman-Kohlhagen delta of the library's own Greeks at the computed strike and the
//       pivot volatility equals the quoted delta (independent code path: the strike comes from the
//       closed-form inverse, the delta from garman_kohlhagen_greeks);
//   (b) the ATM strike has zero straddle delta;
//   (c) the smile evaluated at the three strikes returns the three pivot volatilities, and the
//       ATM, risk reversal and butterfly recomputed from them equal the input quotes.
// Control: measuring delta in the OTHER convention must break (a) wherever r_f T is not tiny, so
// the check can detect a convention mix-up.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "fxval/core/limits.hpp"
#include "support/smile_test_support.hpp"

namespace {

using fxval::pricing::OptionType;
using fxval::smile::DeltaConvention;
using fxval::testing::gk_all;
using fxval::testing::make_quotes;
using fxval::testing::make_smile_market;
using fxval::testing::must;

double greeks_delta(const fxval::pricing::GarmanKohlhagenGreeks& g, DeltaConvention c) {
  return c == DeltaConvention::spot ? g.spot_delta : g.forward_delta;
}

struct Worst {
  double delta = 0.0;
  double atm_delta = 0.0;
  double vol_at_pivot = 0.0;
  double quote = 0.0;
  int cases = 0;
  int control_cases = 0;
  double control_min_error = 1e300;
};

Worst run_grid() {
  Worst worst;
  const double spots[] = {0.8, 1.1, 150.0};
  const double rates[][2] = {{0.04, 0.025}, {-0.005, 0.03}, {0.02, 0.02}, {0.08, 0.0}};
  const double expiries[] = {7.0 / 365, 1.0 / 12, 0.25, 0.5, 1.0, 2.0, 5.0};
  const double atms[] = {0.04, 0.08, 0.15, 0.30};
  const double rr_factors[] = {-0.25, -0.10, 0.0, 0.10, 0.25};
  const double bf_factors[] = {0.0, 0.03, 0.10};
  const double deltas[] = {0.25, 0.10};

  for (const double spot : spots) {
    for (const auto& rate : rates) {
      for (const double t : expiries) {
        for (const double atm : atms) {
          for (const double rrf : rr_factors) {
            for (const double bff : bf_factors) {
              for (const double delta : deltas) {
                for (const auto convention : {DeltaConvention::spot, DeltaConvention::forward}) {
                  const double rr = rrf * atm;
                  const double bf = bff * atm;
                  const auto slice = must(fxval::smile::SmileSlice::from_quotes(
                      make_smile_market(spot, rate[0], rate[1], t),
                      make_quotes(atm, rr, bf, delta),
                      convention));
                  const auto& k = slice.pivot_strikes();
                  const auto& v = slice.pivot_volatilities();
                  ++worst.cases;

                  // (a) quoted delta reproduced by the Greeks of the benchmark model.
                  const auto put =
                      gk_all(OptionType::put, spot, k.put, rate[0], rate[1], v.put.value(), t);
                  const auto call =
                      gk_all(OptionType::call, spot, k.call, rate[0], rate[1], v.call.value(), t);
                  worst.delta = std::max({worst.delta,
                                          std::abs(greeks_delta(put, convention) + delta),
                                          std::abs(greeks_delta(call, convention) - delta)});

                  // (b) straddle delta at the ATM strike.
                  const auto at_call =
                      gk_all(OptionType::call, spot, k.atm, rate[0], rate[1], atm, t);
                  const auto at_put =
                      gk_all(OptionType::put, spot, k.atm, rate[0], rate[1], atm, t);
                  worst.atm_delta = std::max(worst.atm_delta,
                                             std::abs(greeks_delta(at_call, convention) +
                                                      greeks_delta(at_put, convention)));

                  // (c) the smile returns the pivots, and the quotes come back.
                  const auto vp =
                      must(slice.vol_at_strike(must(fxval::Strike::from_value(k.put)))).value();
                  const auto va =
                      must(slice.vol_at_strike(must(fxval::Strike::from_value(k.atm)))).value();
                  const auto vc =
                      must(slice.vol_at_strike(must(fxval::Strike::from_value(k.call)))).value();
                  worst.vol_at_pivot = std::max({worst.vol_at_pivot,
                                                 std::abs(vp - v.put.value()),
                                                 std::abs(va - atm),
                                                 std::abs(vc - v.call.value())});
                  worst.quote = std::max({worst.quote,
                                          std::abs(va - atm),
                                          std::abs((vc - vp) - rr),
                                          std::abs(0.5 * (vc + vp) - va - bf)});

                  // Control: the same strikes judged in the wrong convention.
                  if (std::abs(rate[1] * t) > 0.01) {
                    const auto other = convention == DeltaConvention::spot
                                           ? DeltaConvention::forward
                                           : DeltaConvention::spot;
                    const double mismatch = std::abs(greeks_delta(call, other) - delta);
                    worst.control_min_error = std::min(worst.control_min_error, mismatch);
                    ++worst.control_cases;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return worst;
}

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

TEST(V09_DeltaQuoteRoundTrip, QuotesStrikesAndDeltasAgreeToTheCriterion) {
  const Worst w = run_grid();
  RecordProperty("cases", w.cases);
  RecordProperty("worst_delta_error", sci(w.delta));
  RecordProperty("worst_atm_straddle_delta", sci(w.atm_delta));
  RecordProperty("worst_vol_at_pivot", sci(w.vol_at_pivot));
  RecordProperty("worst_quote_error", sci(w.quote));
  EXPECT_EQ(w.cases, 3 * 4 * 7 * 4 * 5 * 3 * 2 * 2);
  EXPECT_LT(w.delta, fxval::tol::quote_roundtrip) << "worst delta error " << sci(w.delta);
  EXPECT_LT(w.atm_delta, fxval::tol::quote_roundtrip) << sci(w.atm_delta);
  EXPECT_LT(w.vol_at_pivot, fxval::tol::quote_roundtrip) << sci(w.vol_at_pivot);
  EXPECT_LT(w.quote, fxval::tol::quote_roundtrip) << sci(w.quote);
}

TEST(V09_DeltaQuoteRoundTrip, ControlWrongConventionIsDetectedInEveryCaseWhereItMatters) {
  const Worst w = run_grid();
  RecordProperty("control_cases", w.control_cases);
  RecordProperty("control_min_mismatch", sci(w.control_min_error));
  ASSERT_GT(w.control_cases, 1000);
  // A mismatch of at least 0.25 (e^{r_f T} - 1) with r_f T > 0.01 is >= 2.5e-3, a million times the
  // criterion; if the smallest observed mismatch were below 1e-4 the test could not see the error.
  EXPECT_GT(w.control_min_error, 1e-4) << sci(w.control_min_error);
}

TEST(V09_DeltaQuoteRoundTrip, TwentyFivePutAndCallAreOnTheExpectedSidesOfTheForward) {
  const auto slice = fxval::testing::make_slice(1.1, 0.04, 0.025, 0.5, 0.08, 0.0, 0.0);
  EXPECT_LT(slice.pivot_strikes().put, slice.forward());
  EXPECT_GT(slice.pivot_strikes().call, slice.forward());
}

}  // namespace
