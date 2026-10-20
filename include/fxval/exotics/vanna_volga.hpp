#pragma once

#include <array>
#include <variant>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/barrier_closed_form.hpp"
#include "fxval/smile/smile_slice.hpp"

// Vanna-Volga pricing (docs/model_documentation.md, section 7; ADR 0010).
//
// The idea (Castagna and Mercurio 2007; Wystup 2006): price the exotic with ONE flat volatility,
// then add the cost of hedging its smile risk with the three liquid pivots (25-delta put, ATM call,
// 25-delta call). The hedge is the portfolio of the three pivots that has the same vega, vanna and
// volga as the exotic under the flat volatility; its "smile cost" is the sum of weight times
// (market price - flat-volatility price) of each pivot:
//
//     G w = g_X,        G = [vega, vanna, volga] of the three pivots (a 3 x 3 matrix),
//     price(X) = flat(X) + weight_factor * sum_i w_i (market_i - flat_i).
//
// The flat volatility is the ATM volatility of the smile. All Greeks (of the pivots too) are
// computed by the SAME finite-difference routine on the flat-volatility price, so a pivot
// contract gets the weight vector (1, 0, 0) and the three pivots are reproduced exactly (test
// V-14), whatever the finite-difference error.
//
// The weight factor is where the market variants differ, and for barrier contracts it matters:
//   unweighted              factor 1. Known to overstate the smile adjustment of a knock-out,
//                           because the adjustment is paid even on paths that were knocked out.
//   survival                factor = probability that the contract still lives: the no-touch
//                           probability for knock-outs, no-touch and double no-touch, the touch
//                           probability for knock-ins and one-touch, 1 for vanillas.
//                           THIS IS THE VARIANT THIS PROJECT USES BY DEFAULT.
//   vanilla_less_knock_in   a knock-out as a Vanna-Volga vanilla minus a Vanna-Volga knock-in whose
//                           adjustment is weighted by the touch probability (a decomposition, so
//                           the weights come from the knock-in's Greeks). Defined for
//                           single-barrier vanillas and touch options; not for double no-touch.
// The test V-15 reports how far the three differ.

namespace fxval::exotics {

// A plain vanilla, which is what the pivots are.
struct PlainVanilla {
  pricing::OptionType type;
  Strike strike;
};

using ExoticContract = std::variant<PlainVanilla,
                                    pricing::BarrierVanilla,
                                    pricing::TouchContract,
                                    pricing::DoubleNoTouchContract>;

enum class VannaVolgaVariant : std::uint8_t { unweighted, survival, vanilla_less_knock_in };

struct VannaVolgaPrice {
  double price;                   // Vanna-Volga price
  double flat_vol_price;          // the same contract at the ATM volatility
  double smile_cost;              // sum_i w_i (market_i - flat_i), before weighting
  double weight_factor;           // multiplier applied to the smile cost
  std::array<double, 3> weights;  // hedge weights of (25-delta put, ATM call, 25-delta call)
};

// Flat-volatility price of any contract of this module (continuous monitoring for barriers).
[[nodiscard]] Result<double> flat_volatility_price(const pricing::FxMarket& market,
                                                   Volatility volatility,
                                                   const ExoticContract& contract);

// Vega, vanna and volga of a contract under a flat volatility, by central finite differences of
// flat_volatility_price (per unit decimal volatility, like garman_kohlhagen_greeks).
struct FlatGreeks {
  double vega;
  double vanna;
  double volga;
};
[[nodiscard]] Result<FlatGreeks> flat_volatility_greeks(const pricing::FxMarket& market,
                                                        Volatility volatility,
                                                        const ExoticContract& contract);

// Immutable once built; const members are safe to call from several threads.
class VannaVolga {
public:
  // Errors: those of the smile slice quantities (pivot prices) and of the Greek computation.
  [[nodiscard]] static Result<VannaVolga> create(const smile::SmileSlice& slice);

  [[nodiscard]] const pricing::FxMarket& market() const noexcept { return market_; }
  [[nodiscard]] Volatility flat_volatility() const noexcept { return flat_vol_; }

  // Errors: invalid_argument (a barrier already breached, the variant is not defined for the
  // contract), numerical_failure (singular pivot Greek matrix, non-finite values).
  [[nodiscard]] Result<VannaVolgaPrice> price(const ExoticContract& contract,
                                              VannaVolgaVariant variant) const;

private:
  VannaVolga(const pricing::FxMarket& market,
             Volatility flat_vol,
             std::array<PlainVanilla, 3> pivots,
             std::array<double, 3> smile_costs,
             std::array<FlatGreeks, 3> pivot_greeks)
      : market_(market), flat_vol_(flat_vol), pivots_(pivots), smile_costs_(smile_costs),
        pivot_greeks_(pivot_greeks) {}

  pricing::FxMarket market_;
  Volatility flat_vol_;
  std::array<PlainVanilla, 3> pivots_;
  std::array<double, 3> smile_costs_;  // market - flat price of each pivot
  std::array<FlatGreeks, 3> pivot_greeks_;
};

}  // namespace fxval::exotics
