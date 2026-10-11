#include <iostream>

#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "fxval/version.hpp"

int main() {
  std::cout << "fxval " << fxval::version_string << "\n";

  const auto good = fxval::Volatility::from_percent(8.0);
  if (!good) {
    std::cerr << good.error().message << "\n";
    return 1;
  }
  std::cout << "8% volatility is " << good.value().value() << " as a decimal\n";

  const auto bad = fxval::Volatility::from_decimal(8.0);
  if (bad) {
    std::cerr << "expected the unit mistake to be rejected\n";
    return 1;
  }
  std::cout << "rejected as expected: " << bad.error().message << "\n";

  const fxval::pricing::VanillaFx mkt{
      fxval::Spot::from_value(1.10).value(),
      fxval::Strike::from_value(1.10).value(),
      fxval::Rate::from_percent(3.0).value(),
      fxval::Rate::from_percent(1.0).value(),
      fxval::Years::from_years(1.0).value(),
      good.value(),
  };
  const auto call = fxval::pricing::garman_kohlhagen_price(mkt, fxval::pricing::OptionType::call);
  if (!call) {
    std::cerr << call.error().message << "\n";
    return 1;
  }
  std::cout << "ATM 1Y GK call: " << call.value() << "\n";
  return 0;
}
