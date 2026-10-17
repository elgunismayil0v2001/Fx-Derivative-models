#include <cmath>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/heston_qe.hpp"

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

Result<HestonQuadraticExponential> HestonQuadraticExponential::create(Spot spot,
                                                                      Rate domestic,
                                                                      Rate foreign,
                                                                      Years expiry,
                                                                      models::HestonParams params,
                                                                      double psi_critical) {
  if (expiry.value() <= 0.0) {
    return Error{ErrorCode::invalid_argument, "Monte Carlo needs a strictly positive expiry"};
  }
  if (!(params.xi() > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "the QE scheme needs xi > 0 (use GbmModel or the Euler scheme when xi = 0)"};
  }
  if (std::isnan(psi_critical) || psi_critical < 1.0 || psi_critical > 2.0) {
    return Error{ErrorCode::invalid_argument, "psi_critical must be in [1, 2]"};
  }
  return HestonQuadraticExponential(std::log(spot.value()),
                                    domestic.value() - foreign.value(),
                                    expiry.value(),
                                    params,
                                    psi_critical);
}

}  // namespace fxval::mc
