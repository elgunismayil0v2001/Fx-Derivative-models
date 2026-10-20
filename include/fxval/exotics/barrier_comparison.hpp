#pragma once

#include <optional>

#include "fxval/exotics/vanna_volga.hpp"
#include "fxval/mc/barrier_mc.hpp"
#include "fxval/models/heston.hpp"

// Model comparison for a barrier contract (docs/model_documentation.md, section 7.5; test V-15).
//
// The same contract is priced by
//   * Garman-Kohlhagen at the flat ATM volatility (closed form), the naive benchmark,
//   * Vanna-Volga, all three variants (flat ATM volatility plus the smile adjustment),
//   * Heston, by Monte Carlo with the Brownian-bridge correction, with parameters the caller
//     calibrated to the same smile (the module does not calibrate: the fit quality is the
//     caller's to report).
// The model-risk spread is max - min of {GK flat, Vanna-Volga survival, Heston}. It is reported
// in price units, in volatility points (spread divided by the flat-volatility vega per vol point)
// and in percent of notional. It is a measure of how much the answer depends on the model, not an
// error bar: all three can be wrong in the same direction.
namespace fxval::exotics {

struct ModelComparison {
  double gk_flat;
  double vv_unweighted;
  double vv_survival;
  double vv_vanilla_less_knock_in;  // NaN where the variant is not defined for the contract
  double heston;
  double heston_standard_error;
  double spread;              // max - min of {gk_flat, vv_survival, heston}
  double vega_per_vol_point;  // flat-volatility vega * 0.01 (price units per vol point)
  std::optional<double> spread_vol_points;  // empty when |vega| is too small to divide by
  double spread_percent_notional;
};

// Errors: those of the pricers; invalid_argument for a plain vanilla (not a barrier contract).
[[nodiscard]] Result<ModelComparison> compare_barrier_models(const smile::SmileSlice& slice,
                                                             const models::HestonParams& heston,
                                                             const ExoticContract& contract,
                                                             const mc::McConfig& config);

}  // namespace fxval::exotics
