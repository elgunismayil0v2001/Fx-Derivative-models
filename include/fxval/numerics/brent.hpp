#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "fxval/core/error.hpp"

// Brent's root-finding method (Brent 1973, "Algorithms for Minimization without Derivatives",
// chapter 4): bisection safeguarded inverse quadratic interpolation and secant steps.
//
// Why Brent and not Newton: it needs only a sign change, never leaves the bracket and always
// converges, at worst as fast as bisection. The implied-volatility and strike searches of week 8
// are called thousands of times inside a calibration, and a root finder that can diverge would
// turn one bad parameter set into a failed fit. Header-only because the function is a template
// argument. Tested on its own in tests/unit/test_brent.cpp.

namespace fxval::numerics {

// Finds x in [lo, hi] with f(x) = 0. `f` takes a double and returns Result<double>, so a failing
// evaluation (for example a price that is not finite) stops the search with that error.
//
// Errors: invalid_argument if the bracket is not finite, lo >= hi, or f(lo) and f(hi) have the same
// sign; numerical_failure if f returns a non-finite value; not_converged after max_iter steps.
// The result satisfies |bracket| <= 2 (x_tol + 4 eps |x|) at the return point.
template <class F>
[[nodiscard]] Result<double> brent_root(
    F&& f, double lo, double hi, double x_tol = 1e-14, int max_iter = 200) {
  if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo < hi) || !(x_tol > 0.0)) {
    return Error{ErrorCode::invalid_argument, "brent_root needs a finite bracket lo < hi"};
  }
  const auto evaluate = [&f](double x) -> Result<double> {
    Result<double> y = f(x);
    if (y && !std::isfinite(y.value())) {
      return Error{ErrorCode::numerical_failure, "function value in root search is not finite"};
    }
    return y;
  };

  double a = lo;
  double b = hi;
  const Result<double> fa_result = evaluate(a);
  if (!fa_result) {
    return fa_result.error();
  }
  const Result<double> fb_result = evaluate(b);
  if (!fb_result) {
    return fb_result.error();
  }
  double fa = fa_result.value();
  double fb = fb_result.value();
  if (fa == 0.0) {
    return a;
  }
  if (fb == 0.0) {
    return b;
  }
  if ((fa > 0.0) == (fb > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "root is not bracketed: f(lo) and f(hi) have the same sign"};
  }

  double c = a;
  double fc = fa;
  double d = b - a;
  double e = d;
  constexpr double eps = std::numeric_limits<double>::epsilon();
  for (int iteration = 0; iteration < max_iter; ++iteration) {
    // Keep b as the best estimate and [b, c] as the bracket.
    if ((fb > 0.0) == (fc > 0.0)) {
      c = a;
      fc = fa;
      d = b - a;
      e = d;
    }
    if (std::abs(fc) < std::abs(fb)) {
      a = b;
      b = c;
      c = a;
      fa = fb;
      fb = fc;
      fc = fa;
    }
    const double tol = 2.0 * eps * std::abs(b) + 0.5 * x_tol;
    const double m = 0.5 * (c - b);
    if (std::abs(m) <= tol || fb == 0.0) {
      return b;
    }
    if (std::abs(e) < tol || std::abs(fa) <= std::abs(fb)) {
      d = m;  // bisection
      e = m;
    } else {
      double p = 0.0;
      double q = 0.0;
      const double s = fb / fa;
      if (a == c) {  // secant
        p = 2.0 * m * s;
        q = 1.0 - s;
      } else {  // inverse quadratic interpolation
        const double qq = fa / fc;
        const double r = fb / fc;
        p = s * (2.0 * m * qq * (qq - r) - (b - a) * (r - 1.0));
        q = (qq - 1.0) * (r - 1.0) * (s - 1.0);
      }
      if (p > 0.0) {
        q = -q;
      } else {
        p = -p;
      }
      if (2.0 * p < std::min(3.0 * m * q - std::abs(tol * q), std::abs(e * q))) {
        e = d;
        d = p / q;
      } else {
        d = m;
        e = m;
      }
    }
    a = b;
    fa = fb;
    double move = d;
    if (!(std::abs(d) > tol)) {
      move = m > 0.0 ? tol : -tol;
    }
    b += move;
    const Result<double> next = evaluate(b);
    if (!next) {
      return next.error();
    }
    fb = next.value();
  }
  return Error{ErrorCode::not_converged, "brent_root did not converge within the iteration limit"};
}

}  // namespace fxval::numerics
