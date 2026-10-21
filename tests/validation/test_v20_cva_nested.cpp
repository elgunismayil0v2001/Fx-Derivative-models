// V-20: CVA with exposure revalued by a closed form against CVA with exposure revalued by nested
// (inner) Monte Carlo, on a small case.
//
// Setup: spot 1.10, rd 3%, rf 1%, sigma 10%; hazard 3%, recovery 40%; exposure dates 0.2, 0.4, 0.6,
// 0.8; 20,000 outer paths shared by both estimates (paired: the same outer paths and the same inner
// draws for every M differ only in the number of inner paths). Values are per unit of foreign
// currency.
//   Case 1: a long 1-year ATM-forward call (K 1.10). Its value is never negative, so exposure
//   max(V, 0) = V
//           is linear in the inner average, which is unbiased: nested = closed form in expectation.
//   Case 2: a netting set with a long forward (K 1.00) and a short call (K 1.12), 1 year: the net
//   value changes
//           sign, so max(V + noise, 0) is a convex function of the noisy inner value and the nested
//           CVA is biased UP by an amount that shrinks like 1 / M.
//
// Criteria, fixed before the run (M = inner paths):
//   A. Case 1, M = 400: |paired difference| <= 4 paired standard errors.
//   B. Case 2: for M = 25, 100 and 400 the paired difference is not below -4 standard errors (the
//   nested CVA
//      never undershoots the closed form), and
//   C. CONTROL (the bias must be visible, otherwise the test could not tell a correct nested value
//   from one
//      that ignores the max): the difference at M = 25 is above +4 standard errors, and the
//      difference at M = 400 is less than half of it.
// DISCLOSURE: case 2 was first written as a long forward (K 1.00) and a short call (K 1.12). The
// net value changes sign far from the call's strike, where the call is nearly worthless and its
// inner estimate has almost no noise, so the nested bias was invisible (difference -0.7 standard
// errors at M = 25) and criterion C failed for a reason that had to do with the case and not with
// the code. The case was replaced by the straddle above; the criteria were not changed.
//   D. Case 2 with a large inner sample (M = 400) and the closed form agree to within 3% of the CVA
//   (the bias at
//      this M is small next to the CVA itself).

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

#include "support/credit_test_support.hpp"

namespace {

using fxval::testing::credit_market;
using fxval::testing::forward_trade;
using fxval::testing::must;
using fxval::testing::option_trade;
namespace credit = fxval::credit;
using fxval::pricing::OptionType;

const credit::CreditInputs kInputs{0.03, 0.40, 0.03};

credit::ExposureSettings settings(std::size_t inner) {
  credit::ExposureSettings s;
  s.dates = {0.2, 0.4, 0.6, 0.8};
  s.paths = 20'000;
  s.seed = 777;
  s.inner_paths = inner;
  s.inner_seed = 4242;
  return s;
}

TEST(V20_CvaNested, LongOptionNestedEqualsClosedForm) {
  credit::NettingSet set;
  set.options = {option_trade(OptionType::call, 1.10, 1.0, 0.10, 1.0)};
  const auto analytic =
      must(credit::simulate_netting_set_values(credit_market(), set, settings(0)));
  const auto nested =
      must(credit::simulate_netting_set_values(credit_market(), set, settings(400)));
  const auto d = must(credit::cva_difference(nested, analytic, kInputs));
  const double cva = must(credit::cva_estimate(analytic, kInputs)).cva;
  std::cout << "V-20 case 1 (long call): CVA " << cva << ", nested - closed form " << d.cva
            << " (se " << d.standard_error << ", z " << d.cva / d.standard_error << ")\n";
  EXPECT_LT(std::abs(d.cva), 4.0 * d.standard_error);
}

TEST(V20_CvaNested, NettedSetHasAnUpwardBiasThatShrinksWithTheInnerSample) {
  credit::NettingSet set;
  set.forwards = {forward_trade(2.0, 1.10, 1.0)};
  set.options = {option_trade(OptionType::call, 1.10, 1.0, 0.10, -1.0),
                 option_trade(OptionType::put, 1.10, 1.0, 0.10, -1.0)};
  const auto analytic =
      must(credit::simulate_netting_set_values(credit_market(), set, settings(0)));
  const double cva = must(credit::cva_estimate(analytic, kInputs)).cva;
  std::vector<credit::CvaEstimate> diffs;
  for (const std::size_t m : {std::size_t{25}, std::size_t{100}, std::size_t{400}}) {
    const auto nested =
        must(credit::simulate_netting_set_values(credit_market(), set, settings(m)));
    diffs.push_back(must(credit::cva_difference(nested, analytic, kInputs)));
    std::cout << "V-20 case 2 (forward and short straddle), M = " << m << ": CVA closed form "
              << cva << ", nested - closed form " << diffs.back().cva << " (se "
              << diffs.back().standard_error << ", z "
              << diffs.back().cva / diffs.back().standard_error << ")\n";
    EXPECT_GT(diffs.back().cva, -4.0 * diffs.back().standard_error) << "M " << m;
  }
  EXPECT_GT(diffs[0].cva, 4.0 * diffs[0].standard_error);
  EXPECT_LT(diffs[2].cva, 0.5 * diffs[0].cva);
  EXPECT_LT(std::abs(diffs[2].cva), 0.03 * cva);
}

}  // namespace
