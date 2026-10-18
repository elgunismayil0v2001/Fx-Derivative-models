// V-10: smile arbitrage checks (butterfly within an expiry, calendar across expiries).
//
// Criteria, fixed before the scenario results were looked at:
//   (1) CHECKER IS RIGHT. On every slice of every scenario, on the standardised grid x in [-3, 3]
//       (step 0.05), the standardised implied density from the library's density factor g,
//           D = g exp(-d2^2 / 2) / sqrt(2 pi),   d2 = -k / sqrt(w) - sqrt(w) / 2,
//       equals the same quantity computed from OPTION PRICES: D = K sqrt(w) e^{r_d T} C''(K), with
//       C''(K) a five-point second difference of Garman-Kohlhagen prices that use the smile
//       volatility at each strike. Tolerance 1e-6 + 1e-4 |D|, and the sign of D must agree wherever
//       |D| > 1e-5. This does not rely on g at all, so a wrong formula cannot pass.
//   (2) THEORY SAYS CLEAN: the flat scenario has no butterfly or calendar violation (g = 1 and
//   total
//       variance is proportional to T).
//   (3) THEORY SAYS FLAGGED: calendar_inversion 1W (20%) -> 2W (9%): total variance falls from
//       7.7e-4 to 3.1e-4, so every grid point is a violation; 2W -> 1M (9% flat) has total variance
//       rising from 3.1e-4 to 6.7e-4, so it is clean. The concave scenario (wings 4 vol points
//       below the ATM) must be flagged by the butterfly check (its volatility is negative within
//       one standard deviation of the forward).
//   (4) REPORTED: for every scenario the counts and the innermost violation are recorded (test
//       properties and the table printed below); no criterion is attached to them.
// Disclosure: the first run used a relative strike step of 1e-3 and failed on the concave scenario,
// whose volatility falls from 4% to zero within 0.2 standard deviations, so the stencil's
// truncation error was the problem, not g. A Python check at steps 1e-3, 1e-4 and 1e-5 showed the
// price-based density converging onto the formula (error 1e-3, 1e-7, 1e-9 relative). The step was
// then set to 1e-4 for the whole test. The tolerance and the grid were not changed. The
// OBSERVED-regression test at the end freezes what the first run showed; it is evidence about the
// code on these inputs, not a pre-registered expectation.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "fxval/core/limits.hpp"
#include "support/smile_test_support.hpp"

namespace {

using fxval::smile::check_butterfly;
using fxval::smile::check_calendar;
using fxval::smile::check_surface;
using fxval::smile::CheckGrid;
using fxval::smile::SmileSlice;
using fxval::testing::gk_value;
using fxval::testing::load_scenarios;
using fxval::testing::must;
using fxval::testing::slice_from_row;
using fxval::testing::surface_from_rows;

const auto& scenarios() {
  static const auto table = load_scenarios(FXVAL_SMILE_SCENARIOS_PATH);
  return table;
}

// Call price at log-moneyness k from the smile volatility there; NaN-free: returns false if the
// polynomial is not a usable volatility.
bool call_at(const SmileSlice& slice, double k, double* price) {
  const double vol = slice.point(k).vol;
  if (!(vol > 0.0) || !(vol <= 5.0)) {
    return false;
  }
  const auto& m = slice.market();
  *price = gk_value(fxval::pricing::OptionType::call,
                    m.spot.value(),
                    slice.forward() * std::exp(k),
                    m.domestic.value(),
                    m.foreign.value(),
                    vol,
                    slice.expiry().value());
  return true;
}

TEST(V10_SmileArbitrage, DensityFactorAgreesWithOptionPriceConvexityOnEveryScenarioSlice) {
  int points = 0;
  int negative_points = 0;
  double worst_excess = 0.0;  // largest |D - D_price| / (1e-6 + 1e-4 |D|); must stay <= 1
  for (const auto& [name, rows] : scenarios()) {
    for (const auto& row : rows) {
      const SmileSlice slice = slice_from_row(row);
      const double t = slice.expiry().value();
      for (int i = 0; i <= 120; ++i) {
        const double k = (-3.0 + 0.05 * i) * slice.atm_scale();
        const double h_k = 1e-4;  // relative strike step of the stencil (see the note at the top)
        double c[5];
        bool usable = slice.point(k).vol > 0.0;
        const double strike = slice.forward() * std::exp(k);
        for (int j = -2; j <= 2 && usable; ++j) {
          usable = call_at(slice, std::log(strike * (1.0 + j * h_k) / slice.forward()), &c[j + 2]);
        }
        if (!usable) {
          continue;
        }
        const double h = strike * h_k;
        const double second =
            (-c[4] + 16.0 * c[3] - 30.0 * c[2] + 16.0 * c[1] - c[0]) / (12.0 * h * h);
        const double w = slice.point(k).vol * slice.point(k).vol * t;
        const double d_price =
            strike * std::sqrt(w) * std::exp(slice.market().domestic.value() * t) * second;
        const double g = must(fxval::smile::density_factor(slice, k));
        const double d2 = -k / std::sqrt(w) - 0.5 * std::sqrt(w);
        const double d_formula = g * std::exp(-0.5 * d2 * d2) / std::sqrt(2.0 * M_PI);
        ++points;
        negative_points += d_formula < 0.0 ? 1 : 0;
        worst_excess = std::max(
            worst_excess, std::abs(d_formula - d_price) / (1e-6 + 1e-4 * std::abs(d_formula)));
        if (std::abs(d_formula) > 1e-5) {
          EXPECT_EQ(d_formula > 0.0, d_price > 0.0)
              << name << " " << row.tenor << " x=" << (-3.0 + 0.05 * i);
        }
      }
    }
  }
  RecordProperty("points", points);
  RecordProperty("points_with_negative_density", negative_points);
  EXPECT_GT(points, 1500);
  EXPECT_GT(negative_points, 0) << "no negative density in the data: the sign check has no power";
  EXPECT_LE(worst_excess, 1.0) << "worst ratio to tolerance " << worst_excess;
}

TEST(V10_SmileArbitrage, FlatScenarioIsClean) {
  const auto report = must(check_surface(surface_from_rows(scenarios().at("flat"))));
  EXPECT_TRUE(report.clean());
  for (const auto& b : report.butterfly) {
    EXPECT_NEAR(b.min_density_factor, 1.0, 1e-8);
  }
  for (const auto& c : report.calendar) {
    EXPECT_GT(c.worst_relative_change, 0.0);
  }
}

TEST(V10_SmileArbitrage, CalendarInversionIsFlaggedExactlyWhereTheoryPredicts) {
  const auto& rows = scenarios().at("calendar_inversion");
  ASSERT_EQ(rows.size(), 3u);
  const auto first = must(check_calendar(slice_from_row(rows[0]), slice_from_row(rows[1])));
  EXPECT_EQ(first.violations, first.grid_points);  // 1W 20% -> 2W 9%: falls everywhere
  EXPECT_NEAR(first.worst_relative_change,
              (0.09 * 0.09 * rows[1].expiry) / (0.2 * 0.2 * rows[0].expiry) - 1.0,
              1e-9);
  const auto second = must(check_calendar(slice_from_row(rows[1]), slice_from_row(rows[2])));
  EXPECT_TRUE(second.clean());  // 2W 9% -> 1M 9%: rises everywhere
  const auto surface = must(check_surface(surface_from_rows(rows)));
  EXPECT_FALSE(surface.clean());
  EXPECT_EQ(surface.calendar.size(), 2u);
}

TEST(V10_SmileArbitrage, ConcaveScenarioIsFlaggedByTheButterflyCheck) {
  const auto slice = slice_from_row(scenarios().at("concave").front());
  const auto report = must(check_butterfly(slice));
  EXPECT_GT(report.violations, 0);
  EXPECT_GT(report.non_positive_vol_points, 0);
  EXPECT_FALSE(report.clean());
}

// OBSERVED regression: what the first run showed on the synthetic scenarios (default grid, x in
// [-4, 4], 801 points). Not pre-registered; it freezes the behaviour so a change is noticed.
TEST(V10_SmileArbitrage, ObservedVerdictsOnTheScenarioTable) {
  struct Expect {
    const char* scenario;
    std::vector<int> butterfly;  // violations per slice
    std::vector<int> calendar;   // violations per consecutive pair
  };
  const std::vector<Expect> expected = {
      {"calm_g10", {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}},
      {"emerging", {0, 0, 0, 0, 0}, {0, 0, 41, 86}},
      {"stressed", {0, 0, 0, 0, 0, 0}, {0, 0, 34, 0, 61}},
      {"extreme_skew", {83, 87, 100}, {0, 0}},
      {"flat", {0, 0, 0, 0}, {0, 0, 0}},
  };
  for (const auto& e : expected) {
    const auto report = must(check_surface(surface_from_rows(scenarios().at(e.scenario))));
    ASSERT_EQ(report.butterfly.size(), e.butterfly.size()) << e.scenario;
    for (std::size_t i = 0; i < e.butterfly.size(); ++i) {
      EXPECT_EQ(report.butterfly[i].violations, e.butterfly[i]) << e.scenario << " slice " << i;
    }
    for (std::size_t i = 0; i < e.calendar.size(); ++i) {
      EXPECT_EQ(report.calendar[i].violations, e.calendar[i]) << e.scenario << " pair " << i;
    }
  }
  // Where the calendar violations are: in the wings of the earlier slice (3.5 standard deviations
  // or more from the forward, absolute delta of 5 to 11 percent), not near the money.
  for (const char* name : {"emerging", "stressed"}) {
    const auto report = must(check_surface(surface_from_rows(scenarios().at(name))));
    for (const auto& c : report.calendar) {
      if (c.violations > 0) {
        ASSERT_TRUE(c.innermost.has_value());
        EXPECT_GT(std::abs(c.innermost->standardised), 3.4) << name;
        EXPECT_LT(c.innermost->abs_delta, 0.12) << name;
      }
    }
  }
}

TEST(V10_SmileArbitrage, ReportPerScenario) {
  std::ostringstream table;
  table << std::left << std::setw(20) << "scenario" << std::setw(6) << "tenor" << std::right
        << std::setw(9) << "bfly-viol" << std::setw(9) << "vol<=0" << std::setw(11) << "min g"
        << std::setw(14) << "innermost x" << std::setw(11) << "|delta|" << std::setw(11)
        << "cal-viol" << std::setw(12) << "worst dW/W" << '\n';
  for (const auto& [name, rows] : scenarios()) {
    const auto report = must(check_surface(surface_from_rows(rows)));
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto& b = report.butterfly[i];
      table << std::left << std::setw(20) << name << std::setw(6) << rows[i].tenor << std::right
            << std::setw(9) << b.violations << std::setw(9) << b.non_positive_vol_points
            << std::setw(11) << std::setprecision(4) << b.min_density_factor << std::setw(14)
            << (b.innermost ? std::to_string(b.innermost->standardised).substr(0, 7) : "none")
            << std::setw(11)
            << (b.innermost ? std::to_string(b.innermost->abs_delta).substr(0, 6) : "-");
      if (i > 0) {
        const auto& c = report.calendar[i - 1];
        table << std::setw(11) << c.violations << std::setw(12) << std::setprecision(4)
              << c.worst_relative_change;
      }
      table << '\n';
    }
  }
  std::cout << table.str();
  SUCCEED();
}

}  // namespace
