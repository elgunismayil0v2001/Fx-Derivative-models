#pragma once

#include <cmath>
#include <cstddef>

namespace fxval::mc {

// Running mean and sum of squared deviations (Welford), with an exact-in-structure merge (Chan,
// Golub and LeVeque). One RunningStats per block of paths; the engine merges the blocks in block
// order, so the final numbers do not depend on how many threads produced the blocks.
//
// Why not "sum of x and sum of x^2": the textbook variance formula subtracts two large, nearly
// equal numbers and loses all its digits when the mean is large compared with the spread.
class RunningStats {
public:
  void add(double x) noexcept {
    ++count_;
    const double delta = x - mean_;
    mean_ += delta / static_cast<double>(count_);
    sum_sq_dev_ += delta * (x - mean_);
  }

  // Combine with the statistics of another, disjoint set of observations.
  void merge(const RunningStats& other) noexcept {
    if (other.count_ == 0) {
      return;
    }
    if (count_ == 0) {
      *this = other;
      return;
    }
    const auto n_a = static_cast<double>(count_);
    const auto n_b = static_cast<double>(other.count_);
    const double total = n_a + n_b;
    const double delta = other.mean_ - mean_;
    mean_ += delta * n_b / total;
    sum_sq_dev_ += other.sum_sq_dev_ + delta * delta * n_a * n_b / total;
    count_ += other.count_;
  }

  [[nodiscard]] std::size_t count() const noexcept { return count_; }
  [[nodiscard]] double mean() const noexcept { return mean_; }

  // Unbiased sample variance (needs at least two observations, else 0).
  [[nodiscard]] double variance() const noexcept {
    return count_ < 2 ? 0.0 : sum_sq_dev_ / static_cast<double>(count_ - 1);
  }

  // Standard error of the mean.
  [[nodiscard]] double standard_error() const noexcept {
    return count_ < 2 ? 0.0 : std::sqrt(variance() / static_cast<double>(count_));
  }

private:
  std::size_t count_ = 0;
  double mean_ = 0.0;
  double sum_sq_dev_ = 0.0;
};

}  // namespace fxval::mc
