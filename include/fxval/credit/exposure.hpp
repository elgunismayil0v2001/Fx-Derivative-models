#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

// Exposure simulation for FX trades with one counterparty (docs/model_documentation.md section 10).
//
// One currency pair, spot as the only risk factor, simulated EXACTLY as a geometric Brownian motion
// on a date grid. Values and exposures are in domestic currency, at the date they are observed
// (they are NOT discounted; discounting happens in the CVA). Exposure of a netting set at a date is
// max(sum of trade values, 0): no collateral, no margin period of risk (limitation L-28).
//
// Conventions:
//  * dates are year fractions t_1 < ... < t_n, all > 0; the result also has the row t_0 = 0, where
//    every path has the value at today's spot;
//  * a trade that has matured (remaining time <= 0) has value 0: nothing is exposed once settled;
//  * path p uses random stream p of the seed, so the result is deterministic and independent of any
//    ordering (the same convention as the Monte Carlo engine);
//  * the simulation measure is risk-neutral (drift r_d - r_f) unless `real_world_drift` is given,
//  in
//    which case the spot grows at that rate (the measure for PFE; CVA must use the risk-neutral
//    one).

namespace fxval::credit {

// Receive `notional` units of foreign currency at `maturity`, pay notional * strike domestic.
// Negative notional: the opposite position.
struct FxForwardTrade {
  double notional;
  Strike strike;
  Years maturity;
};

// A European FX option position. Negative quantity is a short position.
struct FxOptionTrade {
  pricing::OptionType type;
  Strike strike;
  Years expiry;
  Volatility volatility;  // revaluation volatility (Garman-Kohlhagen at constant volatility)
  double quantity;        // units of foreign notional
};

struct ExposureMarket {
  ExposureMarket(Spot spot_in,
                 Rate domestic_in,
                 Rate foreign_in,
                 Volatility volatility_in,
                 std::optional<double> drift = std::nullopt)
      : spot(spot_in), domestic(domestic_in), foreign(foreign_in), volatility(volatility_in),
        real_world_drift(drift) {}

  Spot spot;
  Rate domestic;
  Rate foreign;
  Volatility volatility;  // volatility of the simulated spot
  std::optional<double>
      real_world_drift;  // expected log-growth plus sigma^2 / 2; empty: risk-neutral
};

struct NettingSet {
  std::vector<FxForwardTrade> forwards;
  std::vector<FxOptionTrade> options;
};

struct ExposureSettings {
  std::vector<double> dates;  // year fractions, strictly increasing, > 0
  std::size_t paths = 100'000;
  std::uint64_t seed = 0;
  // 0: options are revalued by the Garman-Kohlhagen formula. > 0: by an inner Monte Carlo of that
  // many paths from each simulated spot (nested simulation, slow, used as a cross-check). Forwards
  // are always revalued exactly.
  std::size_t inner_paths = 0;
  std::uint64_t inner_seed = 1;
};

// Values of one position or of a netting set: rows = dates (row 0 is t = 0), columns = paths.
class ValueMatrix {
public:
  ValueMatrix(std::vector<double> dates, std::size_t paths, std::vector<double> values);

  [[nodiscard]] const std::vector<double>& dates() const noexcept {
    return dates_;
  }  // with t_0 = 0
  [[nodiscard]] std::size_t paths() const noexcept { return paths_; }
  [[nodiscard]] std::size_t rows() const noexcept { return dates_.size(); }
  [[nodiscard]] std::span<const double> row(std::size_t i) const noexcept {
    return std::span<const double>(values_).subspan(i * paths_, paths_);
  }
  [[nodiscard]] const std::vector<double>& data() const noexcept { return values_; }

private:
  std::vector<double> dates_;
  std::size_t paths_;
  std::vector<double> values_;
};

// The simulated spot at the dates (row 0 is the current spot).
[[nodiscard]] Result<ValueMatrix> simulate_spot_paths(const ExposureMarket& market,
                                                      const ExposureSettings& settings);

// Value of the whole netting set (the sum of its trades) along each path.
[[nodiscard]] Result<ValueMatrix> simulate_netting_set_values(const ExposureMarket& market,
                                                              const NettingSet& set,
                                                              const ExposureSettings& settings);

// Value of a single forward or option, for the gross (un-netted) exposure.
[[nodiscard]] Result<ValueMatrix> simulate_forward_values(const ExposureMarket& market,
                                                          const FxForwardTrade& trade,
                                                          const ExposureSettings& settings);
[[nodiscard]] Result<ValueMatrix> simulate_option_values(const ExposureMarket& market,
                                                         const FxOptionTrade& trade,
                                                         const ExposureSettings& settings);

// Elementwise sum of matrices with the same shape.
[[nodiscard]] Result<ValueMatrix> sum_values(std::span<const ValueMatrix> parts);

// Exposure profile of a value matrix: for each date, EE = mean of max(V, 0), its standard error,
// and the PFE at `level` (the ceil(n * level)-th smallest exposure; 0 when fewer than n (1 - level)
// paths are exposed). EPE is the time average of EE on the grid (trapezoid).
struct ExposureProfile {
  std::vector<double> dates;  // with t_0 = 0
  std::vector<double> ee;
  std::vector<double> ee_standard_error;
  std::vector<double> pfe;
  double level = 0.0;
  double epe = 0.0;
};
[[nodiscard]] Result<ExposureProfile> exposure_profile(const ValueMatrix& values, double level);

// Closed-form value of a forward at remaining time tau for spot s.
[[nodiscard]] double forward_value(const ExposureMarket& market,
                                   const FxForwardTrade& trade,
                                   double spot,
                                   double tau);

}  // namespace fxval::credit
