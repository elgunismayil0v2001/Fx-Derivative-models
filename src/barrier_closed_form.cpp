#include "fxval/pricing/barrier_closed_form.hpp"

#include <cmath>
#include <limits>
#include <numbers>

#include "fxval/numerics/normal.hpp"

namespace fxval::pricing {
namespace {

using numerics::normal_cdf;

struct Inputs {
  double s;      // spot
  double rd;     // domestic rate
  double rf;     // foreign rate
  double t;      // expiry
  double sigma;  // volatility
};

[[nodiscard]] Result<Inputs> check(const FxMarket& market, Volatility volatility) {
  const double t = market.expiry.value();
  const double sigma = volatility.value();
  if (!(t > 0.0) || !(sigma > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "barrier closed forms need a positive expiry and a positive volatility"};
  }
  return Inputs{market.spot.value(), market.domestic.value(), market.foreign.value(), t, sigma};
}

[[nodiscard]] Result<double> finite_or_error(double value, const char* what) {
  if (!std::isfinite(value)) {
    return Error{ErrorCode::numerical_failure, std::string(what) + " is not finite"};
  }
  return value;
}

[[nodiscard]] bool already_breached(double spot, double barrier, BarrierDirection direction) {
  return direction == BarrierDirection::down ? spot <= barrier : spot >= barrier;
}

constexpr const char* breached_message = "the barrier is already breached by the spot";

// The four building blocks A, B, C, D of Reiner and Rubinstein (Haug's notation).
//   phi = +1 call, -1 put;   eta = +1 down barrier, -1 up barrier;   b = r_d - r_f.
struct Blocks {
  double a;
  double b;
  double c;
  double d;
};

[[nodiscard]] Blocks blocks(
    const Inputs& in, double strike, double barrier, double phi, double eta) {
  const double carry = in.rd - in.rf;
  const double mu = (carry - 0.5 * in.sigma * in.sigma) / (in.sigma * in.sigma);
  const double sigma_sqrt_t = in.sigma * std::sqrt(in.t);
  const double shift = (1.0 + mu) * sigma_sqrt_t;
  const double x1 = std::log(in.s / strike) / sigma_sqrt_t + shift;
  const double x2 = std::log(in.s / barrier) / sigma_sqrt_t + shift;
  const double y1 = std::log(barrier * barrier / (in.s * strike)) / sigma_sqrt_t + shift;
  const double y2 = std::log(barrier / in.s) / sigma_sqrt_t + shift;
  const double foreign_df = std::exp(-in.rf * in.t);  // e^{(b - r) T}
  const double domestic_df = std::exp(-in.rd * in.t);
  const double ratio = barrier / in.s;
  const double pow_mu1 = std::pow(ratio, 2.0 * (mu + 1.0));
  const double pow_mu = std::pow(ratio, 2.0 * mu);
  Blocks out{};
  out.a = phi * in.s * foreign_df * normal_cdf(phi * x1) -
          phi * strike * domestic_df * normal_cdf(phi * x1 - phi * sigma_sqrt_t);
  out.b = phi * in.s * foreign_df * normal_cdf(phi * x2) -
          phi * strike * domestic_df * normal_cdf(phi * x2 - phi * sigma_sqrt_t);
  out.c = phi * in.s * foreign_df * pow_mu1 * normal_cdf(eta * y1) -
          phi * strike * domestic_df * pow_mu * normal_cdf(eta * y1 - eta * sigma_sqrt_t);
  out.d = phi * in.s * foreign_df * pow_mu1 * normal_cdf(eta * y2) -
          phi * strike * domestic_df * pow_mu * normal_cdf(eta * y2 - eta * sigma_sqrt_t);
  return out;
}

// Each of the eight knock-in and eight knock-out cases from its own row of the table (zero rebate).
[[nodiscard]] double knock_in_value(const Blocks& v,
                                    bool call,
                                    bool down,
                                    bool strike_above_barrier) {
  if (call && down) {
    return strike_above_barrier ? v.c : v.a - v.b + v.d;
  }
  if (call && !down) {
    return strike_above_barrier ? v.a : v.b - v.c + v.d;
  }
  if (!call && down) {
    return strike_above_barrier ? v.b - v.c + v.d : v.a;
  }
  return strike_above_barrier ? v.a - v.b + v.d : v.c;
}

[[nodiscard]] double knock_out_value(const Blocks& v,
                                     bool call,
                                     bool down,
                                     bool strike_above_barrier) {
  if (call && down) {
    return strike_above_barrier ? v.a - v.c : v.b - v.d;
  }
  if (call && !down) {
    return strike_above_barrier ? 0.0 : v.a - v.b + v.c - v.d;
  }
  if (!call && down) {
    return strike_above_barrier ? v.a - v.b + v.c - v.d : 0.0;
  }
  return strike_above_barrier ? v.b - v.d : v.a - v.c;
}

// P(min or max of the log spot crosses the barrier before T): first passage of Brownian motion
// with drift nu = r_d - r_f - sigma^2 / 2 over a log distance x > 0.
[[nodiscard]] double touch_prob(const Inputs& in, double barrier, BarrierDirection direction) {
  const double nu = in.rd - in.rf - 0.5 * in.sigma * in.sigma;
  const double sigma_sqrt_t = in.sigma * std::sqrt(in.t);
  if (direction == BarrierDirection::down) {
    const double x = std::log(in.s / barrier);
    return normal_cdf((-x - nu * in.t) / sigma_sqrt_t) +
           std::exp(-2.0 * nu * x / (in.sigma * in.sigma)) *
               normal_cdf((-x + nu * in.t) / sigma_sqrt_t);
  }
  const double x = std::log(barrier / in.s);
  return normal_cdf((-x + nu * in.t) / sigma_sqrt_t) +
         std::exp(2.0 * nu * x / (in.sigma * in.sigma)) *
             normal_cdf((-x - nu * in.t) / sigma_sqrt_t);
}

}  // namespace

Result<BarrierLevel> BarrierLevel::from_value(double level) {
  if (!std::isfinite(level) || !(level > 0.0)) {
    return Error{ErrorCode::invalid_argument, "barrier level must be finite and strictly positive"};
  }
  return BarrierLevel{level};
}

Result<double> barrier_vanilla_price(const FxMarket& market,
                                     Volatility volatility,
                                     const BarrierVanilla& contract) {
  const Result<Inputs> in = check(market, volatility);
  if (!in) {
    return in.error();
  }
  const double barrier = contract.barrier.value();
  const double strike = contract.strike.value();
  if (already_breached(in.value().s, barrier, contract.direction)) {
    return Error{ErrorCode::invalid_argument, breached_message};
  }
  const bool call = contract.type == OptionType::call;
  const bool down = contract.direction == BarrierDirection::down;
  const Blocks v = blocks(in.value(), strike, barrier, call ? 1.0 : -1.0, down ? 1.0 : -1.0);
  const bool above = strike >= barrier;
  const double value = contract.effect == BarrierEffect::knock_in
                           ? knock_in_value(v, call, down, above)
                           : knock_out_value(v, call, down, above);
  return finite_or_error(value, "barrier option price");
}

Result<double> touch_probability(const FxMarket& market,
                                 Volatility volatility,
                                 BarrierLevel barrier,
                                 BarrierDirection direction) {
  const Result<Inputs> in = check(market, volatility);
  if (!in) {
    return in.error();
  }
  if (already_breached(in.value().s, barrier.value(), direction)) {
    return Error{ErrorCode::invalid_argument, breached_message};
  }
  return finite_or_error(touch_prob(in.value(), barrier.value(), direction), "touch probability");
}

Result<double> one_touch_price(const FxMarket& market,
                               Volatility volatility,
                               BarrierLevel barrier,
                               BarrierDirection direction) {
  const Result<double> p = touch_probability(market, volatility, barrier, direction);
  if (!p) {
    return p.error();
  }
  return std::exp(-market.domestic.value() * market.expiry.value()) * p.value();
}

Result<double> no_touch_price(const FxMarket& market,
                              Volatility volatility,
                              BarrierLevel barrier,
                              BarrierDirection direction) {
  const Result<double> p = touch_probability(market, volatility, barrier, direction);
  if (!p) {
    return p.error();
  }
  return std::exp(-market.domestic.value() * market.expiry.value()) * (1.0 - p.value());
}

Result<double> double_no_touch_price(const FxMarket& market,
                                     Volatility volatility,
                                     BarrierLevel lower,
                                     BarrierLevel upper) {
  const Result<Inputs> in = check(market, volatility);
  if (!in) {
    return in.error();
  }
  const double lo = lower.value();
  const double hi = upper.value();
  if (!(lo < hi) || !(in.value().s > lo) || !(in.value().s < hi)) {
    return Error{ErrorCode::invalid_argument,
                 "double no-touch needs lower < spot < upper (strictly inside)"};
  }
  // Brownian motion with drift nu on (0, w), absorbed at both ends: eigenfunction expansion,
  //   P = e^{-a x0 - nu^2 T / (2 sigma^2)} (2 / w) sum_n sin(k x0) e^{-k^2 sigma^2 T / 2}
  //         k (1 - (-1)^n e^{a w}) / (a^2 + k^2),     k = n pi / w,  a = nu / sigma^2.
  const double sigma2 = in.value().sigma * in.value().sigma;
  const double nu = in.value().rd - in.value().rf - 0.5 * sigma2;
  const double a = nu / sigma2;
  const double w = std::log(hi / lo);
  const double x0 = std::log(in.value().s / lo);
  const double pi = std::numbers::pi;
  const double prefactor = std::exp(-a * x0 - nu * nu * in.value().t / (2.0 * sigma2)) * 2.0 / w;
  const double aw = a * w;
  double sum = 0.0;
  bool converged = false;
  constexpr int max_terms = 2'000'000;
  for (int n = 1; n <= max_terms; ++n) {
    const double k = static_cast<double>(n) * pi / w;
    const double decay = std::exp(-0.5 * k * k * sigma2 * in.value().t);
    const double sign = (n % 2 == 0) ? 1.0 : -1.0;
    const double term =
        std::sin(k * x0) * decay * k * (1.0 - sign * std::exp(aw)) / (a * a + k * k);
    sum += term;
    if (decay < 1e-18 * (1.0 + std::abs(sum))) {
      converged = true;
      break;
    }
  }
  if (!converged) {
    return Error{ErrorCode::numerical_failure,
                 "double no-touch series did not converge (barriers too wide for the expiry)"};
  }
  const double probability = prefactor * sum;
  if (!std::isfinite(probability)) {
    return Error{ErrorCode::numerical_failure, "double no-touch probability is not finite"};
  }
  return std::exp(-in.value().rd * in.value().t) * probability;
}

Result<double> barrier_contract_price(const FxMarket& market,
                                      Volatility volatility,
                                      const BarrierContract& contract) {
  if (const auto* vanilla = std::get_if<BarrierVanilla>(&contract)) {
    return barrier_vanilla_price(market, volatility, *vanilla);
  }
  if (const auto* touch = std::get_if<TouchContract>(&contract)) {
    return touch->pays_on_touch
               ? one_touch_price(market, volatility, touch->barrier, touch->direction)
               : no_touch_price(market, volatility, touch->barrier, touch->direction);
  }
  const auto& dnt = std::get<DoubleNoTouchContract>(contract);
  return double_no_touch_price(market, volatility, dnt.lower, dnt.upper);
}

}  // namespace fxval::pricing
