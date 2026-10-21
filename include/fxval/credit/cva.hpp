#pragma once

#include <span>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/credit/exposure.hpp"

// Unilateral credit valuation adjustment with a flat hazard rate, a deterministic discount rate and
// independence between default and exposure (no wrong-way risk: limitation L-29).
//
//   CVA = (1 - R) * integral of DF(t) EE(t) dPD(t),   PD(t) = 1 - exp(-lambda t),   DF(t) =
//   exp(-r_d t)
//
// discretised on the exposure dates t_0 = 0 < t_1 < ... < t_n by the trapezoid rule on the product
// DF * EE over each interval:
//
//   CVA = (1 - R) * sum_{i=1..n} [DF(t_{i-1}) EE(t_{i-1}) + DF(t_i) EE(t_i)] / 2 * (Q(t_{i-1}) -
//   Q(t_i)),
//
// where Q(t) = exp(-lambda t) is the survival probability. The result is in domestic currency
// today.

namespace fxval::credit {

struct CreditInputs {
  double hazard_rate;    // lambda >= 0, per year
  double recovery;       // R in [0, 1)
  double domestic_rate;  // deterministic discount rate (continuously compounded)
};

// Weight c_j such that CVA = sum_j c_j * (exposure at date j), j = 0..n, per unit (1 - R) included.
[[nodiscard]] Result<std::vector<double>> cva_weights(std::span<const double> dates,
                                                      const CreditInputs& inputs);

// CVA of a given expected-exposure profile (dates include t_0 = 0).
[[nodiscard]] Result<double> cva_from_profile(std::span<const double> dates,
                                              std::span<const double> ee,
                                              const CreditInputs& inputs);

struct CvaEstimate {
  double cva;
  double standard_error;  // across paths
};

// CVA of simulated values: the weights applied path by path to max(V, 0), then averaged.
[[nodiscard]] Result<CvaEstimate> cva_estimate(const ValueMatrix& values,
                                               const CreditInputs& inputs);

// Paired difference of two CVAs on the same paths (values must have the same shape): the mean of
// the per-path differences and its standard error.
[[nodiscard]] Result<CvaEstimate> cva_difference(const ValueMatrix& a,
                                                 const ValueMatrix& b,
                                                 const CreditInputs& inputs);

}  // namespace fxval::credit
