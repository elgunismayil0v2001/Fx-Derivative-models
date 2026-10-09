#include <iostream>

#include "fxval/core/units.hpp"
#include "fxval/version.hpp"

int main() {
  std::cout << "fxval " << fxval::version_string << "\n";

  const auto good = fxval::Volatility::from_percent(8.0);
  if (!good) {
    std::cerr << good.error().message << "\n";
    return 1;
  }
  std::cout << "8% volatility is " << good.value().value() << " as a decimal\n";

  // Passing 8 as a decimal is a unit mistake. The library reports it instead of using it.
  const auto bad = fxval::Volatility::from_decimal(8.0);
  if (bad) {
    std::cerr << "expected the unit mistake to be rejected\n";
    return 1;
  }
  std::cout << "rejected as expected: " << bad.error().message << "\n";
  return 0;
}
