#pragma once

#include <compare>

#include "fxval/core/error.hpp"

// Strongly typed market quantities.
//
// Conventions (also in docs/model_documentation.md):
//  - Volatility and Rate are DECIMALS: 0.08 means 8%. Use from_percent() for percent input.
//  - Years is an ACT/365-style year fraction.
//  - Values are validated once, at construction, and are immutable afterwards. Code that takes
//    these types can assume the value is finite and inside the documented range.
//  - The types do not convert to or from double implicitly and cannot be mixed by accident.

namespace fxval {

namespace detail {

template <class Derived>
class StrongDouble {
 public:
  [[nodiscard]] constexpr double value() const noexcept { return value_; }

  friend constexpr auto operator<=>(const StrongDouble&, const StrongDouble&) = default;

 protected:
  explicit constexpr StrongDouble(double value) noexcept : value_(value) {}

 private:
  double value_;
};

}  // namespace detail

// Implied or model volatility as a decimal, in [0, limits::max_volatility].
class Volatility final : public detail::StrongDouble<Volatility> {
 public:
  [[nodiscard]] static Result<Volatility> from_decimal(double sigma);
  [[nodiscard]] static Result<Volatility> from_percent(double percent);
  [[nodiscard]] constexpr double percent() const noexcept { return value() * 100.0; }

 private:
  explicit constexpr Volatility(double sigma) noexcept : StrongDouble(sigma) {}
};

// Continuously compounded interest rate as a decimal, |r| <= limits::max_abs_rate.
// Negative rates are allowed.
class Rate final : public detail::StrongDouble<Rate> {
 public:
  [[nodiscard]] static Result<Rate> from_decimal(double rate);
  [[nodiscard]] static Result<Rate> from_percent(double percent);
  [[nodiscard]] constexpr double percent() const noexcept { return value() * 100.0; }

 private:
  explicit constexpr Rate(double rate) noexcept : StrongDouble(rate) {}
};

// Time to expiry as a year fraction, in [0, limits::max_years].
class Years final : public detail::StrongDouble<Years> {
 public:
  [[nodiscard]] static Result<Years> from_years(double years);

 private:
  explicit constexpr Years(double years) noexcept : StrongDouble(years) {}
};

// Spot FX quoted as domestic per one unit of foreign. Strictly positive.
class Spot final : public detail::StrongDouble<Spot> {
 public:
  [[nodiscard]] static Result<Spot> from_value(double spot);

 private:
  explicit constexpr Spot(double spot) noexcept : StrongDouble(spot) {}
};

// Option strike in domestic currency per unit of foreign currency. Strictly positive.
class Strike final : public detail::StrongDouble<Strike> {
 public:
  [[nodiscard]] static Result<Strike> from_value(double strike);

 private:
  explicit constexpr Strike(double strike) noexcept : StrongDouble(strike) {}
};

// Notional amount in foreign currency. Strictly positive; direction (long or short) is a
// separate property of a trade.
class Notional final : public detail::StrongDouble<Notional> {
 public:
  [[nodiscard]] static Result<Notional> from_value(double notional);

 private:
  explicit constexpr Notional(double notional) noexcept : StrongDouble(notional) {}
};

}  // namespace fxval
