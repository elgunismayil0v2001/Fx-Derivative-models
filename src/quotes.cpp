#include "fxval/smile/quotes.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "fxval/core/limits.hpp"

namespace fxval::smile {

Result<VolSpread> VolSpread::from_decimal(double spread) {
  if (!std::isfinite(spread) || std::abs(spread) > limits::max_volatility) {
    std::ostringstream message;
    message << "volatility spread (decimal) must be finite and in [-" << limits::max_volatility
            << ", " << limits::max_volatility << "], got " << std::setprecision(17) << spread;
    return Error{ErrorCode::invalid_argument, message.str()};
  }
  return VolSpread{spread};
}

Result<VolSpread> VolSpread::from_percent(double percent_points) {
  if (!std::isfinite(percent_points)) {
    return Error{ErrorCode::invalid_argument, "volatility spread (percent) must be finite"};
  }
  return from_decimal(percent_points / 100.0);
}

Result<PivotVolatilities> pivot_volatilities(const DeltaQuotes& quotes) {
  if (std::isnan(quotes.delta) || quotes.delta <= 0.0 || quotes.delta >= 0.5) {
    return Error{ErrorCode::invalid_argument,
                 "quote delta must be in (0, 0.5): the wing options are out of the money"};
  }
  const double atm = quotes.atm.value();
  const double wing_level = atm + quotes.butterfly.value();
  const double half_rr = 0.5 * quotes.risk_reversal.value();

  const Result<Volatility> call = Volatility::from_decimal(wing_level + half_rr);
  const Result<Volatility> put = Volatility::from_decimal(wing_level - half_rr);
  if (!call || !(call.value().value() > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "the quotes imply a call volatility that is not positive (ATM + BF + RR/2)"};
  }
  if (!put || !(put.value().value() > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "the quotes imply a put volatility that is not positive (ATM + BF - RR/2)"};
  }
  return PivotVolatilities{put.value(), quotes.atm, call.value()};
}

DeltaQuotes quotes_from_pivots(const PivotVolatilities& pivots, double delta) {
  const double call = pivots.call.value();
  const double put = pivots.put.value();
  const double atm = pivots.atm.value();
  // The differences of two valid volatilities are always inside the VolSpread range.
  return DeltaQuotes{pivots.atm,
                     VolSpread::from_decimal(call - put).value(),
                     VolSpread::from_decimal(0.5 * (call + put) - atm).value(),
                     delta};
}

}  // namespace fxval::smile
