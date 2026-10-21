#pragma once

// Shared helpers for the counterparty-credit tests.

#include <cmath>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "fxval/credit/cva.hpp"
#include "fxval/credit/exposure.hpp"
#include "support/calibration_test_support.hpp"

namespace fxval::testing {

namespace credit = fxval::credit;

// The market of the reference tables: spot 1.10, rd 3%, rf 1%, sigma 10%.
inline credit::ExposureMarket credit_market(std::optional<double> drift = std::nullopt) {
  return credit::ExposureMarket{must(Spot::from_value(1.10)),
                                must(Rate::from_decimal(0.03)),
                                must(Rate::from_decimal(0.01)),
                                must(Volatility::from_decimal(0.10)),
                                drift};
}

inline credit::FxForwardTrade forward_trade(double notional, double strike, double maturity) {
  return credit::FxForwardTrade{
      notional, must(Strike::from_value(strike)), must(Years::from_years(maturity))};
}

inline credit::FxOptionTrade option_trade(
    pricing::OptionType type, double strike, double expiry, double vol, double quantity) {
  return credit::FxOptionTrade{type,
                               must(Strike::from_value(strike)),
                               must(Years::from_years(expiry)),
                               must(Volatility::from_decimal(vol)),
                               quantity};
}

inline std::vector<double> split_numbers(const std::string& text) {
  std::vector<double> out;
  std::stringstream ss(text);
  std::string cell;
  while (std::getline(ss, cell, ';')) {
    out.push_back(std::stod(cell));
  }
  return out;
}

}  // namespace fxval::testing
