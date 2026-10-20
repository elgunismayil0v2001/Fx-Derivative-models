#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/numerics/least_squares.hpp"
#include "fxval/smile/delta_convention.hpp"
#include "fxval/smile/quotes.hpp"

// Heston calibration to FX smile quotes (docs/model_documentation.md, sections 4.6 to 4.11).
//
// Unknowns: (v0, kappa, theta, xi, rho). Data: for every expiry, three market volatilities (put,
// ATM, call at the quoted delta) at the strikes that smile/SmileSlice derives from the quotes.
// Residual: model implied volatility minus market volatility, in decimals, one per pivot.
//
// What this function does NOT do: it does not impose the Feller condition (the market often
// violates it and the fit should say so, not hide it), it does not weight expiries, and it does
// not regularise. Whatever the data cannot identify is reported (near_bound, standard_errors).

namespace fxval::calibration {

struct CalibrationMarket {
  Spot spot;
  Rate domestic;
  Rate foreign;
  smile::DeltaConvention convention;
};

// Quotes of one expiry.
struct MarketSlice {
  Years expiry;
  smile::DeltaQuotes quotes;
};

// Box bounds for (v0, kappa, theta, xi, rho). The defaults are wide on purpose: they stop the
// optimiser from wandering into regions where the pricer is not meant to be used, not to encode
// a view of the market.
struct ParameterBounds {
  std::array<double, 5> lower{1e-4, 0.05, 1e-4, 0.02, -0.98};
  std::array<double, 5> upper{1.0, 25.0, 1.0, 4.0, 0.98};
};

struct CalibrationSettings {
  int random_starts = 6;  // in addition to the data-driven start
  std::uint64_t seed = 20261019;
  std::optional<models::HestonParams> initial_guess;  // replaces the data-driven start
  numerics::LeastSquaresOptions optimiser;
  ParameterBounds bounds;
  bool compute_standard_errors = true;
};

enum class Pivot : std::uint8_t { put, atm, call };

// One quote and how the calibrated model reproduces it.
struct QuoteFit {
  Years expiry;
  Pivot pivot;
  double strike;
  double market_vol;
  double model_vol;
  double error;  // model - market, decimal volatility
};

// What one optimiser run did. Kept for every start: disagreement between starts is information.
struct StartOutcome {
  std::array<double, 5> start;
  std::array<double, 5> end;
  double cost;  // 1/2 sum of squared residuals at the end
  int iterations;
  numerics::LeastSquaresTermination termination;
  bool failed;  // the run produced an error and has no valid end point
};

struct CalibrationResult {
  models::HestonParams params;
  double rms_error;      // root mean square volatility error over all quotes, decimal
  double max_abs_error;  // largest absolute volatility error, decimal
  std::vector<QuoteFit> fits;
  std::vector<StartOutcome> starts;
  std::size_t best_start = 0;  // index into `starts`
  int evaluations = 0;         // residual evaluations over all starts
  bool converged = false;      // the best start ended at a minimum (not at an iteration limit)
  // True where the parameter is within 0.1% of its range from a bound: the data pushed it to the
  // edge, so its value reflects the bound and should not be quoted as an estimate.
  std::array<bool, 5> near_bound{};
  // Standard errors from (J'J)^{-1} at the solution, scaled by the residual variance; +infinity
  // when the data cannot determine the parameter (including when there are no more quotes than
  // unknowns). Linearised, so indicative only (limitation L-14).
  std::array<double, 5> standard_errors{};
};

// Errors: invalid_argument for no slices, a non-positive expiry, an invalid quote set or bounds
// that do not contain a valid region; not_converged if no start produced a usable fit.
[[nodiscard]] Result<CalibrationResult> calibrate_heston(const CalibrationMarket& market,
                                                         const std::vector<MarketSlice>& slices,
                                                         const CalibrationSettings& settings);

}  // namespace fxval::calibration
