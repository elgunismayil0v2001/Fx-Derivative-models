#include "fxval/core/units.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "fxval/core/limits.hpp"

namespace fxval {

namespace {

Error out_of_domain(std::string_view what, std::string_view domain, double got) {
  std::ostringstream message;
  message << what << " must be " << domain << ", got " << std::setprecision(17) << got;
  return Error{ErrorCode::invalid_argument, message.str()};
}

std::string closed_interval(double low, double high) {
  std::ostringstream text;
  text << "finite and in [" << low << ", " << high << "]";
  return text.str();
}

bool in_closed_interval(double x, double low, double high) {
  return std::isfinite(x) && x >= low && x <= high;
}

}  // namespace

Result<Volatility> Volatility::from_decimal(double sigma) {
  if (!in_closed_interval(sigma, 0.0, limits::max_volatility)) {
    return out_of_domain("volatility (decimal)", closed_interval(0.0, limits::max_volatility),
                         sigma);
  }
  return Volatility{sigma};
}

Result<Volatility> Volatility::from_percent(double percent) {
  if (!std::isfinite(percent)) {
    return out_of_domain("volatility (percent)", "finite", percent);
  }
  return from_decimal(percent / 100.0);
}

Result<Rate> Rate::from_decimal(double rate) {
  if (!in_closed_interval(rate, -limits::max_abs_rate, limits::max_abs_rate)) {
    return out_of_domain("rate (decimal)",
                         closed_interval(-limits::max_abs_rate, limits::max_abs_rate), rate);
  }
  return Rate{rate};
}

Result<Rate> Rate::from_percent(double percent) {
  if (!std::isfinite(percent)) {
    return out_of_domain("rate (percent)", "finite", percent);
  }
  return from_decimal(percent / 100.0);
}

Result<Years> Years::from_years(double years) {
  if (!in_closed_interval(years, 0.0, limits::max_years)) {
    return out_of_domain("time to expiry (years)", closed_interval(0.0, limits::max_years), years);
  }
  return Years{years};
}

Result<Strike> Strike::from_value(double strike) {
  if (!std::isfinite(strike) || strike <= 0.0) {
    return out_of_domain("strike", "finite and strictly positive", strike);
  }
  return Strike{strike};
}

Result<Notional> Notional::from_value(double notional) {
  if (!std::isfinite(notional) || notional <= 0.0) {
    return out_of_domain("notional", "finite and strictly positive", notional);
  }
  return Notional{notional};
}

}  // namespace fxval
