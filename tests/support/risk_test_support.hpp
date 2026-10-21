#pragma once

// Helpers shared by the market-risk tests: reading the simulated return series and building
// simple return generators.

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/calibration_test_support.hpp"

namespace fxval::testing {

// id -> returns in time order, from python/gen_garch_reference.py (columns id,t,return).
inline std::map<std::string, std::vector<double>> load_return_series(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("cannot open " + path);
  }
  std::map<std::string, std::vector<double>> series;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (header) {
      header = false;
      continue;
    }
    std::stringstream ss(line);
    std::string id, t, value;
    std::getline(ss, id, ',');
    std::getline(ss, t, ',');
    std::getline(ss, value, ',');
    series[id].push_back(std::stod(value));
  }
  return series;
}

}  // namespace fxval::testing

// ---------------------------------------------------------------------------------------------
// Simulated returns for the backtest tests (generated here, in C++, from the library's Philox
// streams; the Python reference tables are separate).
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>

#include "fxval/mc/normal_stream.hpp"
#include "fxval/risk/backtest.hpp"
#include "fxval/risk/portfolio.hpp"
#include "fxval/timeseries/volatility_models.hpp"

namespace fxval::testing {

enum class Innovations : std::uint8_t { normal, student_t6 };

// GARCH(1,1) returns with unit-variance innovations; the first `burn` draws are discarded.
inline std::vector<double> simulate_garch_returns(std::size_t n,
                                                  const timeseries::GarchParams& p,
                                                  Innovations innovations,
                                                  std::uint64_t seed,
                                                  std::size_t burn = 500) {
  mc::NormalStream rng(seed, 0);
  double spare = 0.0;
  bool have_spare = false;
  const auto normal = [&]() {
    if (have_spare) {
      have_spare = false;
      return spare;
    }
    double a = 0.0;
    double b = 0.0;
    rng.next_pair(a, b);
    spare = b;
    have_spare = true;
    return a;
  };
  const auto innovation = [&]() {
    if (innovations == Innovations::normal) {
      return normal();
    }
    const double z = normal();
    double chi2 = 0.0;
    for (int i = 0; i < 6; ++i) {
      const double g = normal();
      chi2 += g * g;
    }
    return z / std::sqrt(chi2 / 6.0) * std::sqrt(4.0 / 6.0);  // t(6), scaled to unit variance
  };
  std::vector<double> out;
  out.reserve(n);
  double v = p.unconditional_variance();
  for (std::size_t t = 0; t < n + burn; ++t) {
    const double r = std::sqrt(v) * innovation();
    if (t >= burn) {
      out.push_back(r);
    }
    v = p.omega + p.alpha * r * r + p.beta * v;
  }
  return out;
}

// A spot-only portfolio of `units` of foreign currency at spot 1.10.
inline risk::FxPortfolio spot_portfolio(double units) {
  return risk::FxPortfolio{must(Spot::from_value(1.10)),
                           must(Rate::from_decimal(0.03)),
                           must(Rate::from_decimal(0.01)),
                           units,
                           {}};
}

// ---------------------------------------------------------------------------------------------
// VaR forecasters for rolling backtests. Each returns the VaR (positive loss) for the NEXT day from
// the window of past returns it is given. The GARCH-based ones refit every `refit` calls (the
// forecaster keeps state, but only ever sees past windows).
struct ForecasterState {
  std::size_t calls = 0;
  timeseries::GarchParams params;
  bool fitted = false;
  double static_sigma = 0.0;
};

inline Result<double> sigma_from_garch(std::span<const double> window,
                                       std::size_t refit,
                                       ForecasterState& state,
                                       std::vector<double>* path_out) {
  if (!state.fitted || state.calls % refit == 0) {
    timeseries::GarchFitSettings settings;
    settings.starts = 1;
    settings.standard_errors = false;
    const auto fit = timeseries::fit_garch(window, timeseries::GarchModel::garch, settings);
    if (!fit) {
      return fit.error();
    }
    state.params = fit.value().params;
    state.fitted = true;
  }
  ++state.calls;
  auto path = timeseries::garch_variance_path(window, timeseries::GarchModel::garch, state.params);
  if (!path) {
    return path.error();
  }
  const double sigma = std::sqrt(path.value().back());
  if (path_out != nullptr) {
    *path_out = std::move(path.value());
  }
  return sigma;
}

inline risk::VarForecaster historical_forecaster(const risk::FxPortfolio& pf, double level) {
  return [pf, level](std::span<const double> window) -> Result<double> {
    const auto r = risk::historical_revaluation_var_es(pf, window, level);
    if (!r) {
      return r.error();
    }
    return r.value().var;
  };
}

inline risk::VarForecaster ewma_forecaster(const risk::FxPortfolio& pf,
                                           double level,
                                           double lambda) {
  return [pf, level, lambda](std::span<const double> window) -> Result<double> {
    const auto v = timeseries::ewma_variance(window, lambda);
    if (!v) {
      return v.error();
    }
    const auto r = risk::delta_normal_var_es(pf, std::sqrt(v.value().back()), level);
    if (!r) {
      return r.error();
    }
    return r.value().var;
  };
}

inline risk::VarForecaster garch_normal_forecaster(const risk::FxPortfolio& pf,
                                                   double level,
                                                   std::size_t refit) {
  auto state = std::make_shared<ForecasterState>();
  return [pf, level, refit, state](std::span<const double> window) -> Result<double> {
    const auto sigma = sigma_from_garch(window, refit, *state, nullptr);
    if (!sigma) {
      return sigma.error();
    }
    const auto r = risk::delta_normal_var_es(pf, sigma.value(), level);
    if (!r) {
      return r.error();
    }
    return r.value().var;
  };
}

inline risk::VarForecaster garch_fhs_forecaster(const risk::FxPortfolio& pf,
                                                double level,
                                                std::size_t refit) {
  auto state = std::make_shared<ForecasterState>();
  return [pf, level, refit, state](std::span<const double> window) -> Result<double> {
    std::vector<double> path;
    const auto sigma = sigma_from_garch(window, refit, *state, &path);
    if (!sigma) {
      return sigma.error();
    }
    const auto z = timeseries::standardised_residuals(window, path);
    if (!z) {
      return z.error();
    }
    const auto r = risk::filtered_historical_var_es(pf, z.value(), sigma.value(), level);
    if (!r) {
      return r.error();
    }
    return r.value().var;
  };
}

// Control: the volatility of the first window, never updated.
inline risk::VarForecaster static_forecaster(const risk::FxPortfolio& pf, double level) {
  auto state = std::make_shared<ForecasterState>();
  return [pf, level, state](std::span<const double> window) -> Result<double> {
    if (state->calls++ == 0) {
      double sum = 0.0;
      for (double r : window) {
        sum += r * r;
      }
      state->static_sigma = std::sqrt(sum / static_cast<double>(window.size()));
    }
    const auto r = risk::delta_normal_var_es(pf, state->static_sigma, level);
    if (!r) {
      return r.error();
    }
    return r.value().var;
  };
}

inline risk::PnlFunction pnl_function(const risk::FxPortfolio& pf) {
  return [pf](double r) {
    return risk::pnl_for_log_return(pf, r);
  };
}

}  // namespace fxval::testing
