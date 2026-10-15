#include <cmath>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"

namespace fxval::mc {

Result<GbmModel> GbmModel::create(
    Spot spot, Rate domestic, Rate foreign, Volatility volatility, Years expiry) {
  if (expiry.value() <= 0.0) {
    return Error{ErrorCode::invalid_argument, "Monte Carlo needs a strictly positive expiry"};
  }
  const double sigma = volatility.value();
  const double t = expiry.value();
  return GbmModel(spot.value(),
                  (domestic.value() - foreign.value() - 0.5 * sigma * sigma) * t,
                  sigma * std::sqrt(t));
}

Result<HestonEulerFullTruncation> HestonEulerFullTruncation::create(
    Spot spot, Rate domestic, Rate foreign, Years expiry, models::HestonParams params) {
  if (expiry.value() <= 0.0) {
    return Error{ErrorCode::invalid_argument, "Monte Carlo needs a strictly positive expiry"};
  }
  return HestonEulerFullTruncation(
      std::log(spot.value()), domestic.value() - foreign.value(), expiry.value(), params);
}

}  // namespace fxval::mc
