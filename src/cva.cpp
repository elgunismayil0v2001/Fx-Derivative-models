#include "fxval/credit/cva.hpp"

#include <algorithm>
#include <cmath>

namespace fxval::credit {
namespace {

[[nodiscard]] Result<bool> check(std::span<const double> dates, const CreditInputs& in) {
  if (dates.size() < 2 || dates.front() != 0.0) {
    return Error{ErrorCode::invalid_argument,
                 "the dates must start at 0 and have at least one more"};
  }
  for (std::size_t i = 1; i < dates.size(); ++i) {
    if (!(dates[i] > dates[i - 1]) || !std::isfinite(dates[i])) {
      return Error{ErrorCode::invalid_argument, "the dates must be strictly increasing"};
    }
  }
  if (!(in.hazard_rate >= 0.0) || !std::isfinite(in.hazard_rate)) {
    return Error{ErrorCode::invalid_argument, "the hazard rate must be finite and not negative"};
  }
  if (std::isnan(in.recovery) || in.recovery < 0.0 || in.recovery >= 1.0) {
    return Error{ErrorCode::invalid_argument, "the recovery rate must be in [0, 1)"};
  }
  if (!std::isfinite(in.domestic_rate)) {
    return Error{ErrorCode::invalid_argument, "the discount rate must be finite"};
  }
  return true;
}

}  // namespace

Result<std::vector<double>> cva_weights(std::span<const double> dates, const CreditInputs& in) {
  if (const auto ok = check(dates, in); !ok) {
    return ok.error();
  }
  const std::size_t n = dates.size() - 1;
  std::vector<double> dq(n + 2, 0.0);  // dq[i] = Q(t_{i-1}) - Q(t_i), i = 1..n
  for (std::size_t i = 1; i <= n; ++i) {
    dq[i] = std::exp(-in.hazard_rate * dates[i - 1]) - std::exp(-in.hazard_rate * dates[i]);
  }
  std::vector<double> weights(n + 1);
  for (std::size_t j = 0; j <= n; ++j) {
    weights[j] =
        (1.0 - in.recovery) * 0.5 * std::exp(-in.domestic_rate * dates[j]) * (dq[j] + dq[j + 1]);
  }
  return weights;
}

Result<double> cva_from_profile(std::span<const double> dates,
                                std::span<const double> ee,
                                const CreditInputs& in) {
  if (ee.size() != dates.size()) {
    return Error{ErrorCode::invalid_argument, "one exposure per date is needed"};
  }
  const auto w = cva_weights(dates, in);
  if (!w) {
    return w.error();
  }
  double total = 0.0;
  for (std::size_t j = 0; j < ee.size(); ++j) {
    total += w.value()[j] * ee[j];
  }
  return total;
}

namespace {

[[nodiscard]] CvaEstimate summarise(const std::vector<double>& per_path) {
  const auto n = static_cast<double>(per_path.size());
  double sum = 0.0;
  for (const double x : per_path) {
    sum += x;
  }
  const double mean = sum / n;
  double ss = 0.0;
  for (const double x : per_path) {
    ss += (x - mean) * (x - mean);
  }
  return CvaEstimate{mean, std::sqrt(ss / (n - 1.0) / n)};
}

}  // namespace

Result<CvaEstimate> cva_estimate(const ValueMatrix& values, const CreditInputs& in) {
  const auto w = cva_weights(values.dates(), in);
  if (!w) {
    return w.error();
  }
  if (values.paths() < 2) {
    return Error{ErrorCode::invalid_argument, "at least two paths are needed"};
  }
  if (!std::all_of(
          values.data().begin(), values.data().end(), [](double x) { return std::isfinite(x); })) {
    return Error{ErrorCode::invalid_argument, "a simulated value is not finite"};
  }
  std::vector<double> per_path(values.paths(), 0.0);
  for (std::size_t j = 0; j < values.rows(); ++j) {
    for (std::size_t p = 0; p < values.paths(); ++p) {
      per_path[p] += w.value()[j] * std::max(values.row(j)[p], 0.0);
    }
  }
  return summarise(per_path);
}

Result<CvaEstimate> cva_difference(const ValueMatrix& a,
                                   const ValueMatrix& b,
                                   const CreditInputs& in) {
  if (a.paths() != b.paths() || a.rows() != b.rows()) {
    return Error{ErrorCode::invalid_argument, "value matrices differ in shape"};
  }
  const auto w = cva_weights(a.dates(), in);
  if (!w) {
    return w.error();
  }
  if (a.paths() < 2) {
    return Error{ErrorCode::invalid_argument, "at least two paths are needed"};
  }
  std::vector<double> per_path(a.paths(), 0.0);
  for (std::size_t j = 0; j < a.rows(); ++j) {
    for (std::size_t p = 0; p < a.paths(); ++p) {
      per_path[p] += w.value()[j] * (std::max(a.row(j)[p], 0.0) - std::max(b.row(j)[p], 0.0));
    }
  }
  return summarise(per_path);
}

}  // namespace fxval::credit
