#pragma once

#include <span>

#include "fxval/core/error.hpp"

// Value at Risk and Expected Shortfall from a distribution of profit and loss
// (docs/model_documentation.md section 9.4).
//
// Sign convention for the whole risk layer: P&L is positive for a gain; VaR and ES are reported as
// POSITIVE LOSSES (a VaR of 3 means "a loss of 3"). A negative VaR means the quantile of the P&L is
// itself a gain (for example a long-gamma position over a short horizon).
//
// Definitions (fixed here, and implemented a second time in python/gen_risk_reference.py):
//   level alpha in (0, 1); tail probability q = 1 - alpha.
//   historical:  sort the n scenario P&Ls ascending, k = ceil(n q), VaR = -pnl_(k),
//                ES = -mean(pnl_(1..k)).  (The k-th worst outcome; no interpolation.)
//   normal:      P&L ~ N(mean, sd^2): VaR = -(mean + sd z_q), ES = -mean + sd phi(z_q) / q,
//                z_q the q-quantile of the standard normal (negative for small q).

namespace fxval::risk {

struct VarEs {
  double var;
  double es;
};

// Errors: invalid_argument for an empty span, a non-finite P&L or a level outside (0, 1).
[[nodiscard]] Result<VarEs> historical_var_es(std::span<const double> pnl, double level);

// Errors: invalid_argument for a non-finite mean, a negative or non-finite sd, or a bad level.
// sd = 0 is allowed and gives VaR = ES = -mean.
[[nodiscard]] Result<VarEs> normal_var_es(double mean, double sd, double level);

// Cornish-Fisher VaR from the first four moments of the P&L (Cornish and Fisher 1937):
//   z_cf = z + (z^2 - 1) s / 6 + (z^3 - 3 z) k / 24 - (2 z^3 - 5 z) s^2 / 36,
// with z the q-quantile of the standard normal, s the skewness and k the excess kurtosis;
// VaR = -(mean + sd z_cf). It is an expansion around the normal: reliable for moderate skewness
// and kurtosis, and it can be non-monotone in q far in the tail (limitation L-25).
// Errors: invalid_argument for non-finite moments, sd < 0 or a bad level.
[[nodiscard]] Result<double> cornish_fisher_var(
    double mean, double sd, double skewness, double excess_kurtosis, double level);

}  // namespace fxval::risk
