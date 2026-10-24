#pragma once

// Example application: one FX portfolio on one currency pair, valued and risk-measured with the
// models of the library (docs/portfolio_mathematics.md says what is computed and why).
//
//   market data  ->  volatility surface (smile)  ->  value and Greeks per position and in total
//                                                 ->  model cross-checks (flat ATM, Heston)
//                                                 ->  spot ladder (full revaluation vs Taylor)
//                                                 ->  VaR / ES, five methods, GARCH volatility
//                                                 ->  exposure profile, CVA and netting benefit
//
// Everything is in domestic currency (USD for EURUSD), one unit is one unit of foreign currency
// (EUR). The default scenario is synthetic: invented market data and an invented return history, so
// nothing here is a statement about a real book. Uses only the public library API.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::example {

// ---- input ----------------------------------------------------------------------------------

struct SmileQuote {
  double expiry;         // years
  double atm;            // decimal volatility
  double risk_reversal;  // decimal, 25-delta
  double butterfly;      // decimal, 25-delta
};

struct ForwardInput {
  std::string label;
  double notional;  // units of foreign currency received at maturity; negative: the opposite
  double strike;    // domestic units paid per unit of foreign currency
  double maturity;  // years
};

struct OptionInput {
  std::string label;
  pricing::OptionType type;
  double strike;
  double expiry;    // years
  double quantity;  // units of foreign notional; negative: short
};

struct Scenario {
  std::string pair = "EURUSD";
  double spot = 1.0850;
  double domestic_rate = 0.045;  // USD
  double foreign_rate = 0.028;   // EUR
  std::vector<SmileQuote> smile;
  std::vector<ForwardInput> forwards;
  std::vector<OptionInput> options;

  // Market risk
  double var_level = 0.99;
  std::size_t history_days =
      1500;  // synthetic history length (about six years) (ignored when `returns` is set)
  std::vector<double> returns;  // daily log returns; empty: a synthetic GARCH history
  std::uint64_t seed = 20261023;
  double ladder_range = 0.10;  // spot ladder from -range to +range
  std::size_t ladder_points = 21;
  std::size_t histogram_bins = 40;

  // Counterparty credit risk (one netting set, one counterparty, no collateral)
  double hazard_rate = 0.03;  // per year; the credit spread is about hazard * (1 - recovery)
  double recovery = 0.40;
  std::size_t exposure_paths = 20000;
  std::size_t exposure_dates = 24;  // equally spaced up to the longest maturity
  double pfe_level = 0.95;
  // Sections can be switched off (the report keeps them empty): the tests use this to run only what
  // they check. The HTML page expects all of them.
  bool run_market_risk = true;
  bool run_credit = true;
  bool run_heston = true;  // calibrate Heston to the surface and reprice (cross-check)
};

// A small, invented book (long forward, a risk reversal, a short straddle, a long put) and an
// invented EURUSD surface with 1M, 3M, 6M and 1Y quotes.
[[nodiscard]] Scenario default_scenario();

// Reads daily log returns from text: one number per line, blank lines and lines starting with '#'
// skipped, a first line that is not a number is a header. Errors: parse_error.
[[nodiscard]] Result<std::vector<double>> parse_returns(const std::string& text);

// A deterministic GARCH(1,1) history with the given length (daily log returns of about 0.55%
// standard deviation), generated with the library's Philox normal stream.
[[nodiscard]] std::vector<double> synthetic_returns(std::size_t days, std::uint64_t seed);

// ---- output ---------------------------------------------------------------------------------

struct GreekSet {
  double delta = 0.0;         // dV/dS, units of foreign currency
  double gamma = 0.0;         // d2V/dS2
  double vega = 0.0;          // dV/dsigma per 1.00 of volatility (divide by 100 for a vol point)
  double vanna = 0.0;         // d delta / d sigma
  double volga = 0.0;         // d vega / d sigma
  double theta = 0.0;         // dV/dt per year (divide by 365 for a day)
  double rho_domestic = 0.0;  // dV/d r_d per 1.00 (divide by 10000 for a basis point)
  double rho_foreign = 0.0;   // dV/d r_f
};

struct PositionReport {
  std::string label;
  std::string kind;  // "forward", "call" or "put"
  double quantity = 0.0;
  double strike = 0.0;
  double expiry = 0.0;
  double volatility = 0.0;               // smile volatility at the strike (0 for a forward)
  double value = 0.0;                    // domestic currency
  GreekSet greeks;                       // already multiplied by the quantity
  std::optional<double> value_flat_atm;  // same option, one flat ATM volatility
  std::optional<double> value_heston;    // same option, calibrated Heston
};

struct LadderPoint {
  double shock = 0.0;  // simple return of the spot
  double spot = 0.0;
  double value = 0.0;
  double pnl = 0.0;         // full revaluation
  double pnl_taylor = 0.0;  // delta S + gamma S^2 / 2 around today's spot
};

struct VarRow {
  std::string method;
  double var = 0.0;  // positive loss
  double es = 0.0;
};

struct Histogram {
  double lower = 0.0;
  double upper = 0.0;
  std::vector<std::size_t> counts;
};

struct GarchReport {
  bool synthetic = true;
  std::size_t observations = 0;
  double omega = 0.0;
  double alpha = 0.0;
  double beta = 0.0;
  double persistence = 0.0;
  double sigma_next = 0.0;           // next-day volatility forecast, daily
  double sigma_unconditional = 0.0;  // daily
  double log_likelihood = 0.0;
  bool converged = false;
};

struct MarketRiskReport {
  double level = 0.0;
  double portfolio_delta_units = 0.0;  // what the risk layer sees as a spot position
  GarchReport garch;
  std::vector<VarRow> methods;
  Histogram pnl_histogram;  // historical-simulation P&L scenarios
  double historical_var = 0.0;
};

struct StandaloneCva {
  std::string label;
  double cva = 0.0;
  double standard_error = 0.0;
};

struct CreditReport {
  double hazard_rate = 0.0;
  double recovery = 0.0;
  std::size_t paths = 0;
  double exposure_volatility = 0.0;  // the constant volatility of the simulated spot
  double pfe_level = 0.0;
  std::vector<double> dates;
  std::vector<double> ee;
  std::vector<double> pfe;
  double epe = 0.0;
  double cva = 0.0;  // netted
  double cva_standard_error = 0.0;
  double cva_from_profile = 0.0;
  std::vector<StandaloneCva> standalone;
  double standalone_sum = 0.0;
  double netting_benefit = 0.0;  // standalone_sum - cva
};

struct HestonReport {
  double v0 = 0.0;
  double kappa = 0.0;
  double theta = 0.0;
  double xi = 0.0;
  double rho = 0.0;
  double rms_vol_error = 0.0;  // decimal volatility
  bool converged = false;
};

struct Report {
  std::string pair;
  double spot = 0.0;
  double domestic_rate = 0.0;
  double foreign_rate = 0.0;
  std::vector<PositionReport> positions;
  double total_value = 0.0;
  GreekSet total_greeks;
  std::optional<HestonReport> heston;
  std::vector<LadderPoint> ladder;
  MarketRiskReport market_risk;
  CreditReport credit;
};

// Runs everything. Errors are those of the library calls (invalid inputs, failed fits).
[[nodiscard]] Result<Report> analyse(const Scenario& scenario);

// Total value of the scenario's book at another spot (volatilities, rates and dates unchanged),
// computed position by position. The same function produces the ladder; tests use it as the
// reference for finite differences.
[[nodiscard]] Result<double> revalue(const Scenario& scenario, double spot);

}  // namespace fxval::example
