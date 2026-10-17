// Monte Carlo benchmark: thread scaling and cost-adjusted efficiency of the estimators.
//
//   cmake --preset release -DFXVAL_BUILD_BENCH=ON && cmake --build --preset release
//   ./build/release/bench/fxval_bench_mc [csv_path]
//
// This is a measurement tool, not a test: timings depend on the machine, so nothing here is
// asserted. It prints (and optionally writes as CSV) what was measured and on what hardware.
//
//  Part 1: thread scaling. Heston QE, 16 steps, 1,000,000 paths, 1/2/4/8/hardware threads,
//          median of 5 runs. Reports paths per second, speed-up and efficiency against 1 thread,
//          and whether the result is bitwise identical to the 1-thread result (V-07 again).
//          Thread counts above the number of hardware threads are marked: they cannot speed up.
//  Part 2: scheme cost. One thread, same paths: Euler (full truncation) and QE, 16 steps.
//  Part 3: work-normalised variance. For plain, antithetic, control variate and both, the
//          measured wall time t and standard error SE of one run; the figure of merit is
//          SE^2 * t (smaller is better): halving the variance for the same time halves it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/mc/engine.hpp"
#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/variance_reduction.hpp"

namespace {

using Clock = std::chrono::steady_clock;

template <class T>
T must(fxval::Result<T> r) {
  if (!r) {
    std::fprintf(stderr, "error: %s\n", r.error().message.c_str());
    std::exit(1);
  }
  return std::move(r).value();
}

double seconds_since(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

constexpr auto call = [](double s) noexcept {
  return std::max(s - 1.10, 0.0);
};
constexpr auto forward_control = [](double s) noexcept {
  return s;
};

}  // namespace

int main(int argc, char** argv) {
  using namespace fxval;
  const auto params = must(models::HestonParams::create(0.04, 2.0, 0.04, 0.3, -0.5));
  const auto spot = must(Spot::from_value(1.10));
  const auto rd = must(Rate::from_decimal(0.03));
  const auto rf = must(Rate::from_decimal(0.01));
  const auto years = must(Years::from_years(1.0));
  const auto qe = must(mc::HestonQuadraticExponential::create(spot, rd, rf, years, params));
  const auto euler = must(mc::HestonEulerFullTruncation::create(spot, rd, rf, years, params));
  const double forward = 1.10 * std::exp(0.02);

  const unsigned hardware = std::max(1U, std::thread::hardware_concurrency());
  std::printf("fxval Monte Carlo benchmark\n");
  std::printf("hardware threads: %u\n", hardware);
#ifndef NDEBUG
  std::printf("WARNING: this is not an optimised build; the numbers below are meaningless.\n");
#endif

  std::ofstream csv;
  if (argc > 1) {
    csv.open(argv[1]);
    csv << "part,label,threads,paths,seconds,paths_per_second,speedup,efficiency,identical,se,se2_"
           "times_seconds\n";
  }

  // ---- Part 1: thread scaling -------------------------------------------------------------
  constexpr std::size_t kPaths = 1'000'000;
  constexpr std::size_t kSteps = 16;
  constexpr int kRepeats = 5;
  std::vector<unsigned> thread_counts = {1, 2, 4, 8};
  if (std::find(thread_counts.begin(), thread_counts.end(), hardware) == thread_counts.end()) {
    thread_counts.push_back(hardware);
  }
  std::printf("\nPart 1: thread scaling, Heston QE, %zu steps, %zu paths, median of %d runs\n",
              kSteps,
              kPaths,
              kRepeats);
  std::printf("%8s %12s %14s %9s %11s %10s\n",
              "threads",
              "seconds",
              "paths/s",
              "speedup",
              "efficiency",
              "identical");
  double base_rate = 0.0;
  std::uint64_t base_bits = 0;
  for (const unsigned threads : thread_counts) {
    mc::McConfig config;
    config.seed = 20261016;
    config.paths = kPaths;
    config.steps = kSteps;
    config.threads = threads;
    std::vector<double> times;
    double mean = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
      const auto start = Clock::now();
      mean = must(mc::run_monte_carlo(qe, call, config)).mean;
      times.push_back(seconds_since(start));
    }
    const double t = median(times);
    const double rate = static_cast<double>(kPaths) / t;
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof mean);
    std::memcpy(&bits, &mean, sizeof bits);
    if (threads == 1) {
      base_rate = rate;
      base_bits = bits;
    }
    const double speedup = rate / base_rate;
    const double efficiency = speedup / static_cast<double>(threads);
    const bool identical = bits == base_bits;
    std::printf("%8u %12.3f %14.0f %9.2f %10.0f%% %10s%s\n",
                threads,
                t,
                rate,
                speedup,
                100.0 * efficiency,
                identical ? "yes" : "NO",
                threads > hardware ? "   (more threads than cores)" : "");
    if (csv.is_open()) {
      csv << "scaling,qe," << threads << ',' << kPaths << ',' << t << ',' << rate << ',' << speedup
          << ',' << efficiency << ',' << (identical ? 1 : 0) << ",,\n";
    }
  }

  // ---- Part 2: scheme cost ----------------------------------------------------------------
  std::printf("\nPart 2: cost of one path, 1 thread, %zu steps, %zu paths, median of %d runs\n",
              kSteps,
              kPaths,
              kRepeats);
  mc::McConfig single;
  single.seed = 20261016;
  single.paths = kPaths;
  single.steps = kSteps;
  single.threads = 1;
  const auto time_scheme = [&](const auto& model) {
    std::vector<double> times;
    for (int i = 0; i < kRepeats; ++i) {
      const auto start = Clock::now();
      static_cast<void>(must(mc::run_monte_carlo(model, call, single)));
      times.push_back(seconds_since(start));
    }
    return median(times);
  };
  const double t_euler = time_scheme(euler);
  const double t_qe = time_scheme(qe);
  std::printf("%-26s %10.3f s  %12.0f paths/s  %10.2f ns/step\n",
              "Euler full truncation",
              t_euler,
              static_cast<double>(kPaths) / t_euler,
              1e9 * t_euler / static_cast<double>(kPaths * kSteps));
  std::printf("%-26s %10.3f s  %12.0f paths/s  %10.2f ns/step  (%.2fx Euler)\n",
              "QE",
              t_qe,
              static_cast<double>(kPaths) / t_qe,
              1e9 * t_qe / static_cast<double>(kPaths * kSteps),
              t_qe / t_euler);
  if (csv.is_open()) {
    csv << "scheme,euler,1," << kPaths << ',' << t_euler << ','
        << static_cast<double>(kPaths) / t_euler << ",,,,,\n";
    csv << "scheme,qe,1," << kPaths << ',' << t_qe << ',' << static_cast<double>(kPaths) / t_qe
        << ",,,,,\n";
  }

  // ---- Part 3: work-normalised variance ---------------------------------------------------
  std::printf("\nPart 3: SE^2 x time at equal path simulations (QE, ATM call, 1 thread, median of "
              "%d runs)\n",
              kRepeats);
  std::printf("%-22s %10s %14s %16s %12s\n",
              "estimator",
              "seconds",
              "std error",
              "SE^2 x seconds",
              "vs plain");
  struct Row {
    const char* name;
    double seconds;
    double se;
  };
  std::vector<Row> rows;
  const auto time_run = [&](const char* name, auto&& run) {
    std::vector<double> times;
    double se = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
      const auto start = Clock::now();
      se = run().standard_error;
      times.push_back(seconds_since(start));
    }
    rows.push_back({name, median(times), se});
  };
  mc::McConfig pairs = single;
  pairs.paths = kPaths / 2;  // one pair costs two path simulations
  const double c_plain = must(mc::estimate_control_coefficient(
      qe, call, forward_control, false, single.seed, kSteps, 20'000));
  const double c_pair = must(mc::estimate_control_coefficient(
      qe, call, forward_control, true, single.seed, kSteps, 20'000));
  time_run("plain", [&] { return must(mc::run_monte_carlo(qe, call, single)); });
  time_run("antithetic (pairs)", [&] { return must(mc::run_antithetic(qe, call, pairs)); });
  time_run("control variate", [&] {
    return must(
        mc::run_variance_reduced(qe, call, forward_control, forward, c_plain, false, single));
  });
  time_run("antithetic + control", [&] {
    return must(mc::run_variance_reduced(qe, call, forward_control, forward, c_pair, true, pairs));
  });
  const double plain_merit = rows[0].se * rows[0].se * rows[0].seconds;
  for (const Row& r : rows) {
    const double merit = r.se * r.se * r.seconds;
    std::printf("%-22s %10.3f %14.3e %16.3e %11.2fx\n",
                r.name,
                r.seconds,
                r.se,
                merit,
                plain_merit / merit);
    if (csv.is_open()) {
      csv << "efficiency," << r.name << ",1," << kPaths << ',' << r.seconds << ",,,,," << r.se
          << ',' << merit << '\n';
    }
  }
  std::printf("\n('vs plain' = how many times less SE^2 x time than plain: above 1 means the "
              "technique pays for its cost.\n"
              " The control coefficient pilot (20,000 paths) is not included in the timings.)\n");
  return 0;
}
