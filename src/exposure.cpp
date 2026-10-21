#include "fxval/credit/exposure.hpp"

#include <algorithm>
#include <cmath>

#include "fxval/mc/normal_stream.hpp"

namespace fxval::credit {
namespace {

constexpr std::size_t kMaxPaths = 50'000'000;
constexpr std::size_t kMaxDates = 10'000;

[[nodiscard]] Result<bool> check_settings(const ExposureSettings& s) {
  if (s.paths < 2 || s.paths > kMaxPaths) {
    return Error{ErrorCode::invalid_argument, "paths must be between 2 and 50 million"};
  }
  if (s.dates.empty() || s.dates.size() > kMaxDates) {
    return Error{ErrorCode::invalid_argument, "between 1 and 10000 exposure dates are needed"};
  }
  double previous = 0.0;
  for (const double t : s.dates) {
    if (!(t > previous) || !std::isfinite(t)) {
      return Error{ErrorCode::invalid_argument,
                   "exposure dates must be finite, positive and strictly increasing"};
    }
    previous = t;
  }
  return true;
}

[[nodiscard]] std::vector<double> dates_with_origin(const std::vector<double>& dates) {
  std::vector<double> all;
  all.reserve(dates.size() + 1);
  all.push_back(0.0);
  all.insert(all.end(), dates.begin(), dates.end());
  return all;
}

// Draws one standard normal at a time from a stream (a block gives two).
class NormalSource {
public:
  NormalSource(std::uint64_t seed, std::uint64_t stream) noexcept : stream_(seed, stream) {}
  double next() noexcept {
    if (have_second_) {
      have_second_ = false;
      return second_;
    }
    double first = 0.0;
    stream_.next_pair(first, second_);
    have_second_ = true;
    return first;
  }

private:
  mc::NormalStream stream_;
  double second_ = 0.0;
  bool have_second_ = false;
};

[[nodiscard]] double payoff(pricing::OptionType type, double spot, double strike) noexcept {
  return type == pricing::OptionType::call ? std::max(spot - strike, 0.0)
                                           : std::max(strike - spot, 0.0);
}

}  // namespace

ValueMatrix::ValueMatrix(std::vector<double> dates, std::size_t paths, std::vector<double> values)
    : dates_(std::move(dates)), paths_(paths), values_(std::move(values)) {
  FXVAL_EXPECTS(values_.size() == dates_.size() * paths_);
}

double forward_value(const ExposureMarket& market,
                     const FxForwardTrade& trade,
                     double spot,
                     double tau) {
  if (tau <= 0.0) {
    return 0.0;
  }
  return trade.notional * (spot * std::exp(-market.foreign.value() * tau) -
                           trade.strike.value() * std::exp(-market.domestic.value() * tau));
}

Result<ValueMatrix> simulate_spot_paths(const ExposureMarket& market,
                                        const ExposureSettings& settings) {
  if (const auto ok = check_settings(settings); !ok) {
    return ok.error();
  }
  const std::vector<double> dates = dates_with_origin(settings.dates);
  const std::size_t rows = dates.size();
  const double sigma = market.volatility.value();
  const double mu =
      market.real_world_drift.value_or(market.domestic.value() - market.foreign.value());
  std::vector<double> values(rows * settings.paths);
  for (std::size_t p = 0; p < settings.paths; ++p) {
    NormalSource rng(settings.seed, p);
    double s = market.spot.value();
    values[p] = s;
    for (std::size_t i = 1; i < rows; ++i) {
      const double dt = dates[i] - dates[i - 1];
      s *= std::exp((mu - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * rng.next());
      values[i * settings.paths + p] = s;
    }
  }
  return ValueMatrix(dates, settings.paths, std::move(values));
}

Result<ValueMatrix> simulate_forward_values(const ExposureMarket& market,
                                            const FxForwardTrade& trade,
                                            const ExposureSettings& settings) {
  auto spots = simulate_spot_paths(market, settings);
  if (!spots) {
    return spots.error();
  }
  const ValueMatrix& sp = spots.value();
  std::vector<double> values(sp.data().size());
  for (std::size_t i = 0; i < sp.rows(); ++i) {
    const double tau = trade.maturity.value() - sp.dates()[i];
    for (std::size_t p = 0; p < sp.paths(); ++p) {
      values[i * sp.paths() + p] = forward_value(market, trade, sp.row(i)[p], tau);
    }
  }
  return ValueMatrix(sp.dates(), sp.paths(), std::move(values));
}

Result<ValueMatrix> simulate_option_values(const ExposureMarket& market,
                                           const FxOptionTrade& trade,
                                           const ExposureSettings& settings) {
  auto spots = simulate_spot_paths(market, settings);
  if (!spots) {
    return spots.error();
  }
  const ValueMatrix& sp = spots.value();
  const std::size_t rows = sp.rows();
  const double rd = market.domestic.value();
  const double rf = market.foreign.value();
  const double vol = trade.volatility.value();
  std::vector<double> values(sp.data().size(), 0.0);
  for (std::size_t i = 0; i < rows; ++i) {
    const double tau = trade.expiry.value() - sp.dates()[i];
    if (tau <= 0.0) {
      continue;
    }
    const auto remaining = Years::from_years(tau);
    if (!remaining) {
      return remaining.error();
    }
    for (std::size_t p = 0; p < sp.paths(); ++p) {
      const double s = sp.row(i)[p];
      double v = 0.0;
      if (settings.inner_paths == 0 || i == 0) {
        const auto spot = Spot::from_value(s);
        if (!spot) {
          return spot.error();
        }
        const auto price = pricing::garman_kohlhagen_price(pricing::VanillaFx{spot.value(),
                                                                              trade.strike,
                                                                              market.domestic,
                                                                              market.foreign,
                                                                              remaining.value(),
                                                                              trade.volatility},
                                                           trade.type);
        if (!price) {
          return price.error();
        }
        v = price.value();
      } else {
        NormalSource rng(settings.inner_seed, static_cast<std::uint64_t>(p) * rows + i);
        const double drift = (rd - rf - 0.5 * vol * vol) * tau;
        const double spread = vol * std::sqrt(tau);
        double sum = 0.0;
        for (std::size_t k = 0; k < settings.inner_paths; ++k) {
          sum +=
              payoff(trade.type, s * std::exp(drift + spread * rng.next()), trade.strike.value());
        }
        v = std::exp(-rd * tau) * sum / static_cast<double>(settings.inner_paths);
      }
      values[i * sp.paths() + p] = trade.quantity * v;
    }
  }
  return ValueMatrix(sp.dates(), sp.paths(), std::move(values));
}

Result<ValueMatrix> sum_values(std::span<const ValueMatrix> parts) {
  if (parts.empty()) {
    return Error{ErrorCode::invalid_argument, "nothing to sum"};
  }
  std::vector<double> total = parts[0].data();
  for (std::size_t k = 1; k < parts.size(); ++k) {
    if (parts[k].paths() != parts[0].paths() || parts[k].rows() != parts[0].rows()) {
      return Error{ErrorCode::invalid_argument, "value matrices differ in shape"};
    }
    for (std::size_t j = 0; j < total.size(); ++j) {
      total[j] += parts[k].data()[j];
    }
  }
  return ValueMatrix(parts[0].dates(), parts[0].paths(), std::move(total));
}

Result<ValueMatrix> simulate_netting_set_values(const ExposureMarket& market,
                                                const NettingSet& set,
                                                const ExposureSettings& settings) {
  if (set.forwards.empty() && set.options.empty()) {
    return Error{ErrorCode::invalid_argument, "the netting set has no trades"};
  }
  std::vector<ValueMatrix> parts;
  for (const auto& f : set.forwards) {
    auto v = simulate_forward_values(market, f, settings);
    if (!v) {
      return v.error();
    }
    parts.push_back(std::move(v).value());
  }
  for (const auto& o : set.options) {
    auto v = simulate_option_values(market, o, settings);
    if (!v) {
      return v.error();
    }
    parts.push_back(std::move(v).value());
  }
  return sum_values(parts);
}

Result<ExposureProfile> exposure_profile(const ValueMatrix& values, double level) {
  if (std::isnan(level) || level <= 0.0 || level >= 1.0) {
    return Error{ErrorCode::invalid_argument, "the PFE level must be strictly inside (0, 1)"};
  }
  if (values.paths() < 2) {
    return Error{ErrorCode::invalid_argument, "at least two paths are needed"};
  }
  const std::size_t n = values.paths();
  ExposureProfile profile;
  profile.dates = values.dates();
  profile.level = level;
  std::vector<double> exposure(n);
  for (std::size_t i = 0; i < values.rows(); ++i) {
    double sum = 0.0;
    for (std::size_t p = 0; p < n; ++p) {
      exposure[p] = std::max(values.row(i)[p], 0.0);
      sum += exposure[p];
    }
    const double mean = sum / static_cast<double>(n);
    double ss = 0.0;
    for (const double e : exposure) {
      ss += (e - mean) * (e - mean);
    }
    profile.ee.push_back(mean);
    profile.ee_standard_error.push_back(
        std::sqrt(ss / static_cast<double>(n - 1) / static_cast<double>(n)));
    // k-th smallest, k = ceil(n level) with a guard against a product that lands just above an
    // integer
    auto k = static_cast<std::size_t>(std::ceil(static_cast<double>(n) * level - 1e-9));
    k = std::clamp<std::size_t>(k, 1, n);
    std::nth_element(
        exposure.begin(), exposure.begin() + static_cast<std::ptrdiff_t>(k - 1), exposure.end());
    profile.pfe.push_back(exposure[k - 1]);
  }
  const double horizon = profile.dates.back();
  double area = 0.0;
  for (std::size_t i = 1; i < profile.dates.size(); ++i) {
    area += 0.5 * (profile.ee[i - 1] + profile.ee[i]) * (profile.dates[i] - profile.dates[i - 1]);
  }
  profile.epe = horizon > 0.0 ? area / horizon : 0.0;
  return profile;
}

}  // namespace fxval::credit
