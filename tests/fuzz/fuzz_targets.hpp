#pragma once

// Fuzz targets: each takes arbitrary bytes, drives one part of the library and returns an empty
// string when every invariant held, or a description of the violation. Two front ends use them:
//   * libFuzzer entry points (tests/fuzz/fuzz_*.cpp), built with -DFXVAL_BUILD_FUZZERS=ON under
//   clang;
//   * a deterministic replay test (tests/fuzz/test_fuzz_replay.cpp) that runs a seed corpus and a
//   fixed
//     number of seeded mutations in every ordinary build, including the sanitizer builds.
//
// Invariants, not just "does not crash": a call that reports success must return finite numbers
// that satisfy what the model says must hold (bounds, parity, round trips, orderings). A call that
// refuses is always fine. Input bytes are read two ways, chosen by the first byte: "wild" (raw
// 64-bit patterns, so NaN, infinities, zero, negatives and denormals appear) and "sane" (scaled
// into plausible ranges, to reach the numerics).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "fxval/core/sha256.hpp"
#include "fxval/core/units.hpp"
#include "fxval/credit/cva.hpp"
#include "fxval/credit/exposure.hpp"
#include "fxval/pricing/barrier_closed_form.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "fxval/pricing/implied_volatility.hpp"
#include "fxval/risk/backtest.hpp"
#include "fxval/risk/var.hpp"
#include "fxval/smile/delta_convention.hpp"

namespace fxval::fuzz {

class Bytes {
public:
  Bytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  [[nodiscard]] bool wild() const noexcept { return size_ > 0 && (data_[0] & 1U) == 0U; }

  std::uint8_t u8() noexcept { return pos_ < size_ ? data_[pos_++] : std::uint8_t{0}; }

  double raw() noexcept {
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
      bits |= static_cast<std::uint64_t>(u8()) << (8 * i);
    }
    double x = 0.0;
    std::memcpy(&x, &bits, sizeof x);
    return x;
  }

  double range(double lo, double hi) noexcept { return lo + (hi - lo) * (u8() / 255.0); }

  // A value in [lo, hi] in sane mode, any bit pattern in wild mode.
  double pick(double lo, double hi) noexcept { return wild() ? raw() : range(lo, hi); }

  [[nodiscard]] std::size_t remaining() const noexcept { return size_ > pos_ ? size_ - pos_ : 0; }

private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 1;  // the first byte only selects the mode
};

// Number of inputs that got past input validation into the numerical code, per process; the replay
// test uses it to show that the targets are not vacuous (most wild inputs are refused at
// construction).
inline std::size_t& reached() {
  static std::size_t count = 0;
  return count;
}

inline std::string fail(const std::string& what, double a = 0.0, double b = 0.0) {
  std::ostringstream out;
  out.precision(17);
  out << what << " (" << a << ", " << b << ")";
  return out.str();
}

inline bool close(double a, double b, double tol) {
  return std::abs(a - b) <= tol;
}

// ---- 1. vanilla: Garman-Kohlhagen price, Greeks, parity, implied volatility
// -----------------------------
inline std::string check_vanilla(const std::uint8_t* data, std::size_t size) {
  Bytes in(data, size);
  const auto spot = Spot::from_value(in.pick(0.01, 100.0));
  const auto strike = Strike::from_value(in.pick(0.01, 100.0));
  const auto rd = Rate::from_decimal(in.pick(-0.2, 0.5));
  const auto rf = Rate::from_decimal(in.pick(-0.2, 0.5));
  const auto expiry = Years::from_years(in.pick(0.001, 30.0));
  const auto vol = Volatility::from_decimal(in.pick(0.001, 3.0));
  if (!spot || !strike || !rd || !rf || !expiry || !vol) {
    return {};
  }
  ++reached();
  const pricing::VanillaFx m{
      spot.value(), strike.value(), rd.value(), rf.value(), expiry.value(), vol.value()};
  const double t = expiry.value().value();
  const double fwd_leg = spot.value().value() * std::exp(-rf.value().value() * t);
  const double strike_leg = strike.value().value() * std::exp(-rd.value().value() * t);
  const double scale = fwd_leg + strike_leg;
  const double tol = 1e-9 * scale + 1e-300;
  std::optional<double> prices[2];
  for (int k = 0; k < 2; ++k) {
    const auto type = k == 0 ? pricing::OptionType::call : pricing::OptionType::put;
    const auto p = pricing::garman_kohlhagen_price(m, type);
    if (!p) {
      continue;
    }
    const double v = p.value();
    if (!std::isfinite(v)) {
      return fail("GK price reported success but is not finite", v);
    }
    const double upper = k == 0 ? fwd_leg : strike_leg;
    const double intrinsic = k == 0 ? fwd_leg - strike_leg : strike_leg - fwd_leg;
    if (v < std::max(intrinsic, 0.0) - tol || v > upper + tol) {
      return fail("GK price outside the no-arbitrage bounds", v, upper);
    }
    prices[k] = v;
    const auto g = pricing::garman_kohlhagen_greeks(m, type);
    if (g) {
      const auto& x = g.value();
      for (const double y : {x.spot_delta,
                             x.forward_delta,
                             x.gamma,
                             x.vega,
                             x.vanna,
                             x.volga,
                             x.theta,
                             x.rho_domestic,
                             x.rho_foreign}) {
        if (!std::isfinite(y)) {
          return fail("a Greek reported success but is not finite", y);
        }
      }
      if (x.gamma < -1e-12 || x.vega < -1e-12) {
        return fail("gamma or vega is negative", x.gamma, x.vega);
      }
    }
    const pricing::VanillaMarket market{m.spot, m.strike, m.domestic, m.foreign, m.expiry};
    const auto iv = pricing::garman_kohlhagen_implied_volatility(market, type, v);
    if (iv) {
      const pricing::VanillaFx r{m.spot, m.strike, m.domestic, m.foreign, m.expiry, iv.value()};
      const auto again = pricing::garman_kohlhagen_price(r, type);
      if (!again) {
        return fail("implied volatility reported success but cannot reprice", v);
      }
      if (!close(again.value(), v, 1e-7 * scale)) {
        return fail("implied volatility does not reprice the option", again.value(), v);
      }
    }
  }
  if (prices[0] && prices[1] &&
      !close(*prices[0] - *prices[1], fwd_leg - strike_leg, 1e-8 * scale)) {
    return fail("put-call parity violated", *prices[0] - *prices[1], fwd_leg - strike_leg);
  }
  return {};
}

// ---- 2. barrier closed forms: bounds, in-out parity, touch probability
// -------------------------------------
inline std::string check_barrier(const std::uint8_t* data, std::size_t size) {
  Bytes in(data, size);
  const auto spot = Spot::from_value(in.pick(0.1, 10.0));
  const auto rd = Rate::from_decimal(in.pick(-0.1, 0.3));
  const auto rf = Rate::from_decimal(in.pick(-0.1, 0.3));
  const auto expiry = Years::from_years(in.pick(0.01, 10.0));
  const auto vol = Volatility::from_decimal(in.pick(0.01, 2.0));
  const auto strike = Strike::from_value(in.pick(0.1, 10.0));
  const auto barrier = pricing::BarrierLevel::from_value(in.pick(0.05, 20.0));
  const bool call = (in.u8() & 1U) != 0U;
  if (!spot || !rd || !rf || !expiry || !vol || !strike || !barrier) {
    return {};
  }
  ++reached();
  const pricing::FxMarket market{spot.value(), rd.value(), rf.value(), expiry.value()};
  const auto direction = barrier.value().value() < spot.value().value()
                             ? pricing::BarrierDirection::down
                             : pricing::BarrierDirection::up;
  const auto type = call ? pricing::OptionType::call : pricing::OptionType::put;
  const double t = expiry.value().value();
  const double scale = spot.value().value() * std::exp(-rf.value().value() * t) +
                       strike.value().value() * std::exp(-rd.value().value() * t);
  const auto ko = pricing::barrier_vanilla_price(
      market,
      vol.value(),
      {type, strike.value(), barrier.value(), direction, pricing::BarrierEffect::knock_out});
  const auto ki = pricing::barrier_vanilla_price(
      market,
      vol.value(),
      {type, strike.value(), barrier.value(), direction, pricing::BarrierEffect::knock_in});
  const auto plain = pricing::garman_kohlhagen_price(
      pricing::VanillaFx{
          spot.value(), strike.value(), rd.value(), rf.value(), expiry.value(), vol.value()},
      type);
  for (const auto* r : {&ko, &ki}) {
    if (*r && (!std::isfinite(r->value()) || r->value() < -1e-9 * scale ||
               r->value() > scale * (1.0 + 1e-9))) {
      return fail("barrier vanilla price is not finite or outside [0, scale]", r->value(), scale);
    }
  }
  if (ko && ki && plain && !close(ko.value() + ki.value(), plain.value(), 1e-6 * scale)) {
    return fail("in-out parity violated", ko.value() + ki.value(), plain.value());
  }
  const auto p = pricing::touch_probability(market, vol.value(), barrier.value(), direction);
  if (p && !(p.value() >= -1e-12 && p.value() <= 1.0 + 1e-12)) {
    return fail("touch probability outside [0, 1]", p.value());
  }
  const auto one = pricing::one_touch_price(market, vol.value(), barrier.value(), direction);
  const auto none = pricing::no_touch_price(market, vol.value(), barrier.value(), direction);
  if (one && none) {
    const double df = std::exp(-rd.value().value() * t);
    if (!close(one.value() + none.value(), df, 1e-9 * (1.0 + df))) {
      return fail(
          "one-touch plus no-touch is not the discount factor", one.value() + none.value(), df);
    }
  }
  return {};
}

// ---- 3. delta to strike round trip
// --------------------------------------------------------------------------
inline std::string check_delta(const std::uint8_t* data, std::size_t size) {
  Bytes in(data, size);
  const auto spot = Spot::from_value(in.pick(0.1, 10.0));
  const auto rd = Rate::from_decimal(in.pick(-0.1, 0.2));
  const auto rf = Rate::from_decimal(in.pick(-0.1, 0.2));
  const auto expiry = Years::from_years(in.pick(0.01, 5.0));
  const auto vol = Volatility::from_decimal(in.pick(0.01, 1.0));
  const double delta = in.pick(0.01, 0.99);
  const bool call = (in.u8() & 1U) != 0U;
  const bool forward = (in.u8() & 1U) != 0U;
  if (!spot || !rd || !rf || !expiry || !vol) {
    return {};
  }
  ++reached();
  const smile::SmileMarket market{spot.value(), rd.value(), rf.value(), expiry.value()};
  const auto type = call ? pricing::OptionType::call : pricing::OptionType::put;
  const auto conv = forward ? smile::DeltaConvention::forward : smile::DeltaConvention::spot;
  const auto k = smile::strike_from_delta(market, vol.value(), delta, type, conv);
  if (!k) {
    return {};
  }
  const double spread = vol.value().value() * std::sqrt(expiry.value().value());
  if (!std::isfinite(k.value()) || k.value() <= 0.0) {
    return fail("strike_from_delta reported success with a non-positive or non-finite strike",
                k.value());
  }
  if (spread < 1e-3 || spread > 6.0 || spot.value().value() < 1e-100 || k.value() < 1e-100) {
    // The round trip is ill-conditioned at the extremes (a denormal spot has no relative precision
    // to lose a round trip against); only the sanity check above applies there.
    return {};
  }
  const auto strike = Strike::from_value(k.value());
  if (!strike) {
    return fail("a returned strike is not a valid Strike", k.value());
  }
  const auto back = smile::delta_at_strike(market, strike.value(), vol.value(), type, conv);
  if (!back) {
    return fail("delta_at_strike refused a strike that strike_from_delta produced", k.value());
  }
  if (!close(std::abs(back.value()), delta, 1e-6)) {
    return fail("delta round trip", std::abs(back.value()), delta);
  }
  return {};
}

// ---- 4. risk: VaR/ES and backtest statistics
// --------------------------------------------------------------
inline std::string check_risk(const std::uint8_t* data, std::size_t size) {
  Bytes in(data, size);
  const std::size_t n = 1 + in.u8() % 64U;
  const double level = in.wild() ? in.raw() : in.range(0.5, 0.9999);
  std::vector<double> pnl(n);
  for (double& x : pnl) {
    x = in.wild() ? in.raw() : in.range(-1e6, 1e6);
  }
  const auto h = risk::historical_var_es(pnl, level);
  if (h) {
    const double lo = *std::min_element(pnl.begin(), pnl.end());
    const double hi = *std::max_element(pnl.begin(), pnl.end());
    const auto [var, es] = h.value();
    const double tol = 1e-9 * (std::abs(lo) + std::abs(hi)) + 1e-300;
    if (!std::isfinite(var) || !std::isfinite(es)) {
      return fail("historical VaR/ES reported success but is not finite", var, es);
    }
    if (var < -hi - tol || var > -lo + tol || es < var - tol || es > -lo + tol) {
      return fail("historical VaR/ES outside the range of the scenarios or ES below VaR", var, es);
    }
  }
  const double mean = in.pick(-1e3, 1e3);
  const double sd = in.pick(0.0, 1e3);
  const auto nv = risk::normal_var_es(mean, sd, level);
  if (nv) {
    const double tol = 1e-9 * (std::abs(nv.value().var) + std::abs(nv.value().es)) + 1e-12;
    if (!std::isfinite(nv.value().var) || !std::isfinite(nv.value().es) ||
        nv.value().es < nv.value().var - tol) {
      return fail("normal VaR/ES is not finite or ES is below VaR", nv.value().var, nv.value().es);
    }
  }
  std::vector<std::uint8_t> hits(1 + in.u8() % 255U);
  for (auto& x : hits) {
    x = (in.u8() & 1U) != 0U ? 1 : 0;
  }
  const auto stats = risk::evaluate_exceptions(hits, level);
  if (stats) {
    const auto& s = stats.value();
    for (const double p :
         {s.kupiec_p_value, s.independence_p_value, s.conditional_p_value, s.binomial_cdf}) {
      if (!(p >= -1e-12 && p <= 1.0 + 1e-12)) {
        return fail("a backtest p-value or cdf is outside [0, 1]", p);
      }
    }
    if (s.exceptions > s.observations || s.kupiec_lr < -1e-9 || s.independence_lr < -1e-9) {
      return fail("backtest counts or statistics are inconsistent",
                  static_cast<double>(s.exceptions),
                  s.kupiec_lr);
    }
  }
  return {};
}

// ---- 5. credit: exposure paths, profile, CVA
// ------------------------------------------------------------------
inline std::string check_credit(const std::uint8_t* data, std::size_t size) {
  Bytes in(data, size);
  const auto spot = Spot::from_value(in.pick(0.1, 10.0));
  const auto rd = Rate::from_decimal(in.pick(-0.1, 0.3));
  const auto rf = Rate::from_decimal(in.pick(-0.1, 0.3));
  const auto vol = Volatility::from_decimal(in.pick(0.01, 2.0));
  const auto strike = Strike::from_value(in.pick(0.1, 10.0));
  const auto maturity = Years::from_years(in.pick(0.05, 5.0));
  const auto opt_vol = Volatility::from_decimal(in.pick(0.01, 2.0));
  const double notional = in.pick(-1e6, 1e6);
  const double opt_qty = in.pick(-1e6, 1e6);
  if (!spot || !rd || !rf || !vol || !strike || !maturity || !opt_vol) {
    return {};
  }
  ++reached();
  credit::ExposureSettings settings;
  const std::size_t dates = 1 + in.u8() % 6U;
  double t = 0.0;
  for (std::size_t i = 0; i < dates; ++i) {
    t += in.wild() ? in.raw() : in.range(0.01, 1.5);
    settings.dates.push_back(t);
  }
  settings.paths = 2 + in.u8() % 40U;
  settings.seed = in.u8();
  settings.inner_paths = (in.u8() % 4U == 0U) ? 1 + in.u8() % 8U : 0U;
  const credit::ExposureMarket market{
      spot.value(), rd.value(), rf.value(), vol.value(), std::nullopt};
  credit::NettingSet set;
  set.forwards.push_back({notional, strike.value(), maturity.value()});
  set.options.push_back(
      {pricing::OptionType::call, strike.value(), maturity.value(), opt_vol.value(), opt_qty});
  const auto values = credit::simulate_netting_set_values(market, set, settings);
  if (!values) {
    return {};
  }
  for (const double v : values.value().data()) {
    if (!std::isfinite(v)) {
      return fail("exposure simulation reported success but a value is not finite", v);
    }
  }
  const double level = in.range(0.5, 0.999);
  const auto profile = credit::exposure_profile(values.value(), level);
  if (!profile) {
    return fail("exposure_profile refused finite values");
  }
  double peak = 0.0;
  for (const double v : values.value().data()) {
    peak = std::max(peak, v);
  }
  const auto& pr = profile.value();
  for (std::size_t i = 0; i < pr.ee.size(); ++i) {
    if (!(pr.ee[i] >= 0.0) || !(pr.pfe[i] >= 0.0) || pr.pfe[i] > peak + 1e-9 * peak ||
        !std::isfinite(pr.ee[i]) || !(pr.ee_standard_error[i] >= 0.0)) {
      return fail(
          "exposure profile violates EE >= 0, 0 <= PFE <= peak or SE >= 0", pr.ee[i], pr.pfe[i]);
    }
  }
  const double r1 = in.range(0.0, 0.5);
  const double r2 = in.range(0.5, 0.99);
  const double lambda = in.range(0.0, 0.5);
  const double disc = in.range(-0.1, 0.3);
  const auto c1 = credit::cva_estimate(values.value(), {lambda, r1, disc});
  const auto c2 = credit::cva_estimate(values.value(), {lambda, r2, disc});
  if (c1 && c2) {
    if (!(c1.value().cva >= 0.0) || !std::isfinite(c1.value().cva) || !(c2.value().cva >= 0.0)) {
      return fail("CVA is negative or not finite", c1.value().cva, c2.value().cva);
    }
    if (c2.value().cva > c1.value().cva * (1.0 + 1e-9) + 1e-300) {
      return fail("CVA is not decreasing in the recovery rate", c1.value().cva, c2.value().cva);
    }
    const double scale = (1.0 - r2) / (1.0 - r1);
    if (!close(c2.value().cva, c1.value().cva * scale, 1e-9 * (1.0 + c1.value().cva))) {
      return fail("CVA is not proportional to 1 - R", c1.value().cva, c2.value().cva);
    }
  }
  return {};
}

// ---- 6. SHA-256 fingerprint
// -----------------------------------------------------------------------------------
inline std::string check_sha256(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const std::string h = sha256_hex(text);
  if (h.size() != 64U || h.find_first_not_of("0123456789abcdef") != std::string::npos) {
    return fail("SHA-256 output is not 64 lower-case hex characters",
                static_cast<double>(h.size()));
  }
  if (sha256_hex(text) != h) {
    return fail("SHA-256 is not deterministic");
  }
  std::string copy(text);
  copy.push_back('\0');
  if (sha256_hex(copy) == h) {
    return fail("appending a byte did not change the hash");
  }
  if (size > 0) {
    copy.assign(text);
    copy[size / 2] = static_cast<char>(copy[size / 2] ^ 1);
    if (sha256_hex(copy) == h) {
      return fail("flipping a bit did not change the hash");
    }
  }
  return {};
}

}  // namespace fxval::fuzz
