#pragma once

#include <cstdint>
#include <variant>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

// Barrier and touch options under Garman-Kohlhagen dynamics (constant rates and volatility),
// CONTINUOUSLY monitored barriers, in closed form. docs/model_documentation.md section 8.
//
//   * Single-barrier vanillas (all eight knock-in / knock-out, call / put, up / down cases):
//     Reiner and Rubinstein (1991), in the form of Haug, "The Complete Guide to Option Pricing
//     Formulas", with zero rebate. Knock-in and knock-out are computed from their OWN formulas,
//     not as vanilla minus the other, so that the in-out parity test (V-13) is a real test.
//   * One-touch and no-touch (cash of one unit of domestic currency, paid at EXPIRY): from the
//     first-passage probability of Brownian motion with drift.
//   * Double no-touch: the sine-series (eigenfunction) solution for Brownian motion between two
//     absorbing barriers.
// Spot and barriers are domestic per one unit of foreign, like the strike. Prices of vanillas are
// per one unit of foreign notional; touch options pay one unit of DOMESTIC currency.
//
// A contract whose barrier is already breached at inception (spot on or beyond the barrier) is
// refused with invalid_argument: it has no well-defined price in this module (a knock-out is worth
// zero and a knock-in is a vanilla; both are decisions for the caller).

namespace fxval::pricing {

enum class BarrierDirection : std::uint8_t { down, up };  // barrier below / above the spot
enum class BarrierEffect : std::uint8_t { knock_out, knock_in };

// A barrier level, strictly positive.
class BarrierLevel final : public detail::StrongDouble<BarrierLevel> {
public:
  [[nodiscard]] static Result<BarrierLevel> from_value(double level);

private:
  explicit constexpr BarrierLevel(double level) noexcept : StrongDouble(level) {}
};

// What an exotic needs from the market: spot, the two flat rates and the expiry. The volatility is
// passed separately because the whole point of the model comparison is that it differs by model.
struct FxMarket {
  Spot spot;
  Rate domestic;
  Rate foreign;
  Years expiry;
};

struct BarrierVanilla {
  OptionType type;
  Strike strike;
  BarrierLevel barrier;
  BarrierDirection direction;
  BarrierEffect effect;
};

// A touch option: one unit of domestic currency at expiry if the barrier is touched (one-touch) or
// not touched (no-touch) during the life.
struct TouchContract {
  BarrierLevel barrier;
  BarrierDirection direction;
  bool pays_on_touch;  // true: one-touch, false: no-touch
};

// One unit of domestic currency at expiry if the spot stays strictly between the two barriers.
struct DoubleNoTouchContract {
  BarrierLevel lower;
  BarrierLevel upper;
};

// Every barrier-type contract of this module, for code that treats them uniformly (Monte Carlo,
// Vanna-Volga, the model comparison).
using BarrierContract = std::variant<BarrierVanilla, TouchContract, DoubleNoTouchContract>;

// Errors: invalid_argument for a non-positive expiry or volatility (the closed forms need
// randomness) and for a barrier already breached; numerical_failure for a non-finite value.
[[nodiscard]] Result<double> barrier_vanilla_price(const FxMarket& market,
                                                   Volatility volatility,
                                                   const BarrierVanilla& contract);

// Risk-neutral (domestic measure) probability that the spot touches the barrier before expiry.
[[nodiscard]] Result<double> touch_probability(const FxMarket& market,
                                               Volatility volatility,
                                               BarrierLevel barrier,
                                               BarrierDirection direction);

// One unit of domestic currency at expiry if the barrier is touched / not touched.
// one_touch + no_touch = discount factor.
[[nodiscard]] Result<double> one_touch_price(const FxMarket& market,
                                             Volatility volatility,
                                             BarrierLevel barrier,
                                             BarrierDirection direction);
[[nodiscard]] Result<double> no_touch_price(const FxMarket& market,
                                            Volatility volatility,
                                            BarrierLevel barrier,
                                            BarrierDirection direction);

// One unit of domestic currency at expiry if the spot stays strictly between `lower` and `upper`
// for the whole life. invalid_argument if lower >= upper or the spot is not strictly inside.
[[nodiscard]] Result<double> double_no_touch_price(const FxMarket& market,
                                                   Volatility volatility,
                                                   BarrierLevel lower,
                                                   BarrierLevel upper);

// Closed-form price of any BarrierContract under flat volatility, by dispatch to the functions
// above. The continuous-monitoring price.
[[nodiscard]] Result<double> barrier_contract_price(const FxMarket& market,
                                                    Volatility volatility,
                                                    const BarrierContract& contract);

}  // namespace fxval::pricing
