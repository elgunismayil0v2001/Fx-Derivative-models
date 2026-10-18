#include "fxval/smile/arbitrage_checks.hpp"

#include <algorithm>
#include <cmath>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::smile {
namespace {

Result<bool> validate(const CheckGrid& grid) {
  if (!(grid.x_max > 0.0) || !std::isfinite(grid.x_max) || grid.points < 3) {
    return Error{ErrorCode::invalid_argument, "check grid needs x_max > 0 and at least 3 points"};
  }
  return true;
}

double grid_x(const CheckGrid& grid, int i) {
  return -grid.x_max +
         2.0 * grid.x_max * static_cast<double>(i) / static_cast<double>(grid.points - 1);
}

// Absolute spot delta of the out-of-the-money option at k, at the slice's own volatility there.
double otm_abs_delta(const SmileSlice& slice, double k, double vol) {
  const auto strike = Strike::from_value(slice.forward() * std::exp(k));
  const auto sigma = Volatility::from_decimal(vol);
  if (!strike || !sigma) {
    return 0.0;
  }
  const auto type = k >= 0.0 ? pricing::OptionType::call : pricing::OptionType::put;
  const auto delta =
      delta_at_strike(slice.market(), strike.value(), sigma.value(), type, DeltaConvention::spot);
  return delta ? std::abs(delta.value()) : 0.0;
}

Violation make_violation(const SmileSlice& slice, double k, double vol, double value) {
  return Violation{k,
                   k / slice.atm_scale(),
                   slice.forward() * std::exp(k),
                   vol > 0.0 ? otm_abs_delta(slice, k, vol) : 0.0,
                   value};
}

void keep_innermost(std::optional<Violation>& current, const Violation& candidate) {
  if (!current || std::abs(candidate.standardised) < std::abs(current->standardised)) {
    current = candidate;
  }
}

}  // namespace

bool SurfaceReport::clean() const noexcept {
  return std::all_of(butterfly.begin(), butterfly.end(), [](const auto& r) { return r.clean(); }) &&
         std::all_of(calendar.begin(), calendar.end(), [](const auto& r) { return r.clean(); });
}

Result<double> density_factor(const SmileSlice& slice, double log_moneyness) {
  const double k = log_moneyness;
  const SmilePoint p = slice.point(k);
  if (!std::isfinite(k) || !(p.vol > 0.0)) {
    return Error{ErrorCode::numerical_failure,
                 "density factor is undefined where the smile polynomial is not positive"};
  }
  const double t = slice.expiry().value();
  const double w = p.vol * p.vol * t;
  const double dw = 2.0 * p.vol * p.dvol_dk * t;
  const double d2w = 2.0 * t * (p.dvol_dk * p.dvol_dk + p.vol * p.d2vol_dk2);
  const double lead = 1.0 - k * dw / (2.0 * w);
  const double g = lead * lead - 0.25 * dw * dw * (1.0 / w + 0.25) + 0.5 * d2w;
  if (!std::isfinite(g)) {
    return Error{ErrorCode::numerical_failure, "density factor is not finite"};
  }
  return g;
}

Result<ButterflyReport> check_butterfly(const SmileSlice& slice, const CheckGrid& grid) {
  if (const auto ok = validate(grid); !ok) {
    return ok.error();
  }
  ButterflyReport report;
  report.grid_points = grid.points;
  bool have_min = false;
  for (int i = 0; i < grid.points; ++i) {
    const double k = grid_x(grid, i) * slice.atm_scale();
    const double vol = slice.point(k).vol;
    if (!(vol > 0.0)) {
      ++report.non_positive_vol_points;
      keep_innermost(report.innermost, make_violation(slice, k, vol, vol));
      continue;
    }
    const Result<double> g = density_factor(slice, k);
    if (!g) {
      return g.error();
    }
    if (!have_min || g.value() < report.min_density_factor) {
      report.min_density_factor = g.value();
      report.min_density_log_moneyness = k;
      have_min = true;
    }
    if (g.value() < -tol::arbitrage_slack) {
      ++report.violations;
      keep_innermost(report.innermost, make_violation(slice, k, vol, g.value()));
    }
  }
  return report;
}

Result<CalendarReport> check_calendar(const SmileSlice& earlier,
                                      const SmileSlice& later,
                                      const CheckGrid& grid) {
  if (const auto ok = validate(grid); !ok) {
    return ok.error();
  }
  if (!(later.expiry().value() > earlier.expiry().value())) {
    return Error{ErrorCode::invalid_argument, "calendar check needs the later expiry second"};
  }
  CalendarReport report;
  report.earlier_expiry = earlier.expiry().value();
  report.later_expiry = later.expiry().value();
  report.grid_points = grid.points;
  bool have_worst = false;
  for (int i = 0; i < grid.points; ++i) {
    const double k = grid_x(grid, i) * earlier.atm_scale();
    const double v1 = earlier.point(k).vol;
    const double v2 = later.point(k).vol;
    if (!(v1 > 0.0) || !(v2 > 0.0)) {
      ++report.undefined_points;
      continue;
    }
    const double w1 = v1 * v1 * report.earlier_expiry;
    const double w2 = v2 * v2 * report.later_expiry;
    const double change = (w2 - w1) / w1;
    if (!have_worst || change < report.worst_relative_change) {
      report.worst_relative_change = change;
      have_worst = true;
    }
    if (change < -tol::arbitrage_slack) {
      ++report.violations;
      keep_innermost(report.innermost, make_violation(earlier, k, v1, change));
    }
  }
  return report;
}

Result<SurfaceReport> check_surface(const VolSurface& surface, const CheckGrid& grid) {
  SurfaceReport report;
  const auto& slices = surface.slices();
  for (const SmileSlice& slice : slices) {
    Result<ButterflyReport> b = check_butterfly(slice, grid);
    if (!b) {
      return b.error();
    }
    report.butterfly.push_back(std::move(b).value());
  }
  for (std::size_t i = 1; i < slices.size(); ++i) {
    Result<CalendarReport> c = check_calendar(slices[i - 1], slices[i], grid);
    if (!c) {
      return c.error();
    }
    report.calendar.push_back(std::move(c).value());
  }
  return report;
}

}  // namespace fxval::smile
