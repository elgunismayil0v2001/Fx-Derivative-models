#include "fxval/mc/barrier_mc.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/normal_stream.hpp"

namespace fxval::mc {
namespace {

using pricing::BarrierDirection;
using pricing::BarrierEffect;
using pricing::OptionType;

// The contract reduced to what the path loop needs: up to two log barriers and how the survival
// probability of the path turns into a payoff.
struct Compiled {
  bool has_lower = false;
  bool has_upper = false;
  double log_lower = 0.0;
  double log_upper = 0.0;
  enum class Payoff : std::uint8_t {
    vanilla_out,
    vanilla_in,
    survive,
    hit
  } payoff = Payoff::survive;
  OptionType type = OptionType::call;
  double strike = 0.0;
};

[[nodiscard]] Result<Compiled> compile(const BarrierContract& contract, double spot) {
  Compiled c;
  const auto breached = []() -> Result<Compiled> {
    return Error{ErrorCode::invalid_argument, "the barrier is already breached by the spot"};
  };
  const double log_spot = std::log(spot);
  const auto set_single = [&](double level, BarrierDirection direction) -> bool {
    const double b = std::log(level);
    if (direction == BarrierDirection::down) {
      c.has_lower = true;
      c.log_lower = b;
      return log_spot > b;
    }
    c.has_upper = true;
    c.log_upper = b;
    return log_spot < b;
  };
  if (const auto* v = std::get_if<pricing::BarrierVanilla>(&contract)) {
    if (!set_single(v->barrier.value(), v->direction)) {
      return breached();
    }
    c.payoff = v->effect == BarrierEffect::knock_out ? Compiled::Payoff::vanilla_out
                                                     : Compiled::Payoff::vanilla_in;
    c.type = v->type;
    c.strike = v->strike.value();
  } else if (const auto* t = std::get_if<TouchContract>(&contract)) {
    if (!set_single(t->barrier.value(), t->direction)) {
      return breached();
    }
    c.payoff = t->pays_on_touch ? Compiled::Payoff::hit : Compiled::Payoff::survive;
  } else {
    const auto& d = std::get<DoubleNoTouchContract>(contract);
    c.has_lower = c.has_upper = true;
    c.log_lower = std::log(d.lower.value());
    c.log_upper = std::log(d.upper.value());
    const bool inside = c.log_lower < log_spot && log_spot < c.log_upper;
    if (!inside) {
      return Error{ErrorCode::invalid_argument,
                   "double no-touch needs lower < spot < upper (strictly inside)"};
    }
  }
  return c;
}

// Survival of the path through one step.
[[nodiscard]] double step_survival(const Compiled& c,
                                   BarrierMonitoring monitoring,
                                   double x0,
                                   double x1,
                                   double variance_dt) noexcept {
  double survive = 1.0;
  const auto one_level = [&](double level, bool lower) noexcept {
    const bool beyond = lower ? (x1 <= level) : (x1 >= level);
    if (beyond) {
      return 0.0;
    }
    if (monitoring == BarrierMonitoring::discrete) {
      return 1.0;
    }
    const double p = std::exp(-2.0 * (x0 - level) * (x1 - level) / variance_dt);
    return 1.0 - std::min(p, 1.0);
  };
  if (c.has_lower) {
    survive *= one_level(c.log_lower, true);
  }
  if (c.has_upper) {
    survive *= one_level(c.log_upper, false);
  }
  return survive;
}

[[nodiscard]] double payoff_of(const Compiled& c, double survival, double terminal_spot) noexcept {
  switch (c.payoff) {
  case Compiled::Payoff::survive:
    return survival;
  case Compiled::Payoff::hit:
    return 1.0 - survival;
  case Compiled::Payoff::vanilla_out:
  case Compiled::Payoff::vanilla_in: {
    const double intrinsic = c.type == OptionType::call ? std::max(terminal_spot - c.strike, 0.0)
                                                        : std::max(c.strike - terminal_spot, 0.0);
    return c.payoff == Compiled::Payoff::vanilla_out ? intrinsic * survival
                                                     : intrinsic * (1.0 - survival);
  }
  }
  return 0.0;
}

[[nodiscard]] Result<McPrice> discount(const Result<McEstimate>& estimate,
                                       const pricing::FxMarket& m) {
  if (!estimate) {
    return Error{estimate.error().code, estimate.error().message};
  }
  const double factor = std::exp(-m.domestic.value() * m.expiry.value());
  const McEstimate& e = estimate.value();
  return McPrice{factor * e.mean, factor * e.standard_error, e.paths};
}

}  // namespace

Result<McPrice> gbm_barrier_mc_price(const pricing::FxMarket& market,
                                     Volatility volatility,
                                     const BarrierContract& contract,
                                     BarrierMonitoring monitoring,
                                     const McConfig& config) {
  const double t = market.expiry.value();
  const double sigma = volatility.value();
  if (!(t > 0.0) || !(sigma > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "barrier Monte Carlo needs a positive expiry and volatility"};
  }
  const Result<Compiled> compiled = compile(contract, market.spot.value());
  if (!compiled) {
    return compiled.error();
  }
  const Compiled c = compiled.value();
  const std::size_t steps = std::max<std::size_t>(config.steps, 1);
  const double dt = t / static_cast<double>(steps);
  const double drift =
      (market.domestic.value() - market.foreign.value() - 0.5 * sigma * sigma) * dt;
  const double vol_step = sigma * std::sqrt(dt);
  const double variance_dt = sigma * sigma * dt;
  const double log_spot0 = std::log(market.spot.value());
  const std::uint64_t seed = config.seed;

  const Result<McEstimate> estimate = run_samples(
      [&](std::uint64_t path) noexcept {
        NormalStream rng(seed, path);
        double x = log_spot0;
        double survival = 1.0;
        for (std::size_t i = 0; i < steps; ++i) {
          double z = 0.0;
          double unused = 0.0;
          rng.next_pair(z, unused);
          const double next = x + drift + vol_step * z;
          survival *= step_survival(c, monitoring, x, next, variance_dt);
          x = next;
          // A knocked-out path pays nothing, so stop early, except for a knock-in, whose payoff
          // is the vanilla of the TERMINAL spot and needs the path to be finished (finding F-14).
          if (survival == 0.0 && c.payoff != Compiled::Payoff::vanilla_in) {
            break;
          }
        }
        return payoff_of(c, survival, std::exp(x));
      },
      config);
  return discount(estimate, market);
}

Result<McPrice> heston_barrier_mc_price(const pricing::FxMarket& market,
                                        const models::HestonParams& params,
                                        const BarrierContract& contract,
                                        BarrierMonitoring monitoring,
                                        const McConfig& config) {
  const Result<Compiled> compiled = compile(contract, market.spot.value());
  if (!compiled) {
    return compiled.error();
  }
  const Compiled c = compiled.value();
  const auto model = HestonQuadraticExponential::create(
      market.spot, market.domestic, market.foreign, market.expiry, params);
  if (!model) {
    return Error{model.error().code, model.error().message};
  }
  const HestonQuadraticExponential& qe = model.value();
  const std::size_t steps = std::max<std::size_t>(config.steps, 1);
  const std::uint64_t seed = config.seed;

  const Result<McEstimate> estimate = run_samples(
      [&](std::uint64_t path) noexcept {
        NormalStream rng(seed, path);
        double survival = 1.0;
        const HestonState end = qe.simulate_path(
            rng, steps, [&](double x0, double x1, double v0, double v1, double dt) noexcept {
              if (survival == 0.0) {
                return;
              }
              const double variance_dt = std::max(0.5 * (v0 + v1) * dt, 1e-300);
              survival *= step_survival(c, monitoring, x0, x1, variance_dt);
            });
        return payoff_of(c, survival, end.spot);
      },
      config);
  return discount(estimate, market);
}

}  // namespace fxval::mc
