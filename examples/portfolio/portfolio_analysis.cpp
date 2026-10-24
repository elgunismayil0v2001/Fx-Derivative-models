#include "portfolio_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <utility>

#include "fxval/calibration/heston_calibration.hpp"
#include "fxval/core/units.hpp"
#include "fxval/credit/cva.hpp"
#include "fxval/credit/exposure.hpp"
#include "fxval/mc/normal_stream.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/risk/portfolio.hpp"
#include "fxval/risk/var.hpp"
#include "fxval/smile/smile_slice.hpp"
#include "fxval/smile/vol_surface.hpp"
#include "fxval/timeseries/volatility_models.hpp"

namespace fxval::example {

namespace {

using pricing::OptionType;

Error invalid(const std::string& what) {
  return Error{ErrorCode::invalid_argument, what};
}

// Everything resolved from the scenario once: validated market, the surface, the volatility of each
// option read from it at today's spot (held fixed under spot shocks, as the risk layer does).
struct ResolvedOption {
  OptionInput input;
  double volatility;
};

struct Book {
  Spot spot;
  Rate domestic;
  Rate foreign;
  smile::VolSurface surface;
  std::vector<ResolvedOption> options;
};

Result<Book> resolve(const Scenario& s) {
  const auto spot = Spot::from_value(s.spot);
  const auto rd = Rate::from_decimal(s.domestic_rate);
  const auto rf = Rate::from_decimal(s.foreign_rate);
  if (!spot) {
    return spot.error();
  }
  if (!rd) {
    return rd.error();
  }
  if (!rf) {
    return rf.error();
  }
  if (s.smile.empty()) {
    return invalid("the scenario has no smile quotes");
  }
  std::vector<smile::SmileSlice> slices;
  for (const SmileQuote& q : s.smile) {
    const auto expiry = Years::from_years(q.expiry);
    const auto atm = Volatility::from_decimal(q.atm);
    const auto rr = smile::VolSpread::from_decimal(q.risk_reversal);
    const auto bf = smile::VolSpread::from_decimal(q.butterfly);
    if (!expiry) {
      return expiry.error();
    }
    if (!atm) {
      return atm.error();
    }
    if (!rr) {
      return rr.error();
    }
    if (!bf) {
      return bf.error();
    }
    const smile::SmileMarket market{spot.value(), rd.value(), rf.value(), expiry.value()};
    const smile::DeltaQuotes quotes{atm.value(), rr.value(), bf.value(), 0.25};
    auto slice = smile::SmileSlice::from_quotes(market, quotes, smile::DeltaConvention::spot);
    if (!slice) {
      return slice.error();
    }
    slices.push_back(std::move(slice).value());
  }
  auto surface = smile::VolSurface::create(std::move(slices));
  if (!surface) {
    return surface.error();
  }
  std::vector<ResolvedOption> options;
  for (const OptionInput& o : s.options) {
    if (!(o.strike > 0.0) || !(o.expiry > 0.0) || !std::isfinite(o.quantity)) {
      return invalid("option '" + o.label + "': strike and expiry must be positive");
    }
    const double forward = s.spot * std::exp((s.domestic_rate - s.foreign_rate) * o.expiry);
    const auto years = Years::from_years(o.expiry);
    if (!years) {
      return years.error();
    }
    const auto vol = surface.value().vol(std::log(o.strike / forward), years.value());
    if (!vol) {
      return Error{vol.error().code, "option '" + o.label + "': " + vol.error().message};
    }
    options.push_back({o, vol.value().value()});
  }
  for (const ForwardInput& f : s.forwards) {
    if (!(f.strike > 0.0) || !(f.maturity > 0.0) || !std::isfinite(f.notional)) {
      return invalid("forward '" + f.label + "': strike and maturity must be positive");
    }
  }
  return Book{spot.value(), rd.value(), rf.value(), std::move(surface).value(), std::move(options)};
}

credit::ExposureMarket exposure_market(const Book& book, double volatility) {
  return credit::ExposureMarket(
      book.spot, book.domestic, book.foreign, Volatility::from_decimal(volatility).value());
}

Result<pricing::VanillaFx> vanilla(const Book& book, const ResolvedOption& o, double spot) {
  const auto s = Spot::from_value(spot);
  const auto k = Strike::from_value(o.input.strike);
  const auto t = Years::from_years(o.input.expiry);
  const auto v = Volatility::from_decimal(o.volatility);
  if (!s) {
    return s.error();
  }
  if (!k) {
    return k.error();
  }
  if (!t) {
    return t.error();
  }
  if (!v) {
    return v.error();
  }
  return pricing::VanillaFx{
      s.value(), k.value(), book.domestic, book.foreign, t.value(), v.value()};
}

// Forward Greeks, by hand (docs/portfolio_mathematics.md section 3): V = N (S e^{-r_f T} - K
// e^{-r_d T}).
GreekSet forward_greeks(const Scenario& s, const ForwardInput& f) {
  const double ef = std::exp(-s.foreign_rate * f.maturity);
  const double ed = std::exp(-s.domestic_rate * f.maturity);
  GreekSet g;
  g.delta = f.notional * ef;
  g.theta = f.notional * (s.foreign_rate * s.spot * ef - s.domestic_rate * f.strike * ed);
  g.rho_domestic = f.notional * f.maturity * f.strike * ed;
  g.rho_foreign = -f.notional * f.maturity * s.spot * ef;
  return g;
}

double forward_value(const Scenario& s, const ForwardInput& f, double spot) {
  return f.notional * (spot * std::exp(-s.foreign_rate * f.maturity) -
                       f.strike * std::exp(-s.domestic_rate * f.maturity));
}

void add(GreekSet& total, const GreekSet& g) {
  total.delta += g.delta;
  total.gamma += g.gamma;
  total.vega += g.vega;
  total.vanna += g.vanna;
  total.volga += g.volga;
  total.theta += g.theta;
  total.rho_domestic += g.rho_domestic;
  total.rho_foreign += g.rho_foreign;
}

Result<double> book_value(const Scenario& s, const Book& book, double spot) {
  double total = 0.0;
  for (const ForwardInput& f : s.forwards) {
    total += forward_value(s, f, spot);
  }
  for (const ResolvedOption& o : book.options) {
    const auto m = vanilla(book, o, spot);
    if (!m) {
      return m.error();
    }
    const auto p = pricing::garman_kohlhagen_price(m.value(), o.input.type);
    if (!p) {
      return p.error();
    }
    total += o.input.quantity * p.value();
  }
  return total;
}

// What the risk layer sees. A forward's P&L under a spot shock is that of N e^{-r_f T} units of
// spot (its value is that plus a constant), so forwards become spot units; options are legs.
Result<risk::FxPortfolio> risk_portfolio(const Scenario& s, const Book& book) {
  risk::FxPortfolio p{book.spot, book.domestic, book.foreign, 0.0, {}};
  for (const ForwardInput& f : s.forwards) {
    p.spot_units += f.notional * std::exp(-s.foreign_rate * f.maturity);
  }
  for (const ResolvedOption& o : book.options) {
    const auto k = Strike::from_value(o.input.strike);
    const auto t = Years::from_years(o.input.expiry);
    const auto v = Volatility::from_decimal(o.volatility);
    if (!k) {
      return k.error();
    }
    if (!t) {
      return t.error();
    }
    if (!v) {
      return v.error();
    }
    p.options.push_back({o.input.type, k.value(), t.value(), v.value(), o.input.quantity});
  }
  return p;
}

double longest_maturity(const Scenario& s) {
  double longest = 0.0;
  for (const ForwardInput& f : s.forwards) {
    longest = std::max(longest, f.maturity);
  }
  for (const OptionInput& o : s.options) {
    longest = std::max(longest, o.expiry);
  }
  return longest;
}

Histogram histogram(const std::vector<double>& values, std::size_t bins) {
  Histogram h;
  h.counts.assign(bins, 0);
  if (values.empty() || bins == 0) {
    return h;
  }
  const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
  h.lower = *lo;
  h.upper = *hi;
  const double width = (h.upper - h.lower) / static_cast<double>(bins);
  for (const double v : values) {
    std::size_t k = width > 0.0 ? static_cast<std::size_t>((v - h.lower) / width) : 0;
    k = std::min(k, bins - 1);
    ++h.counts[k];
  }
  return h;
}

// strtod with the whole cell consumed (trailing blanks allowed).
bool parse_number(const std::string& cell, double& value) {
  char* end = nullptr;
  value = std::strtod(cell.c_str(), &end);
  if (end == cell.c_str()) {
    return false;
  }
  return std::string(end).find_first_not_of(" \t\r") == std::string::npos;
}

template <class T>
bool failed(const Result<T>& r, Error& out) {
  if (r) {
    return false;
  }
  out = r.error();
  return true;
}

}  // namespace

// ---- public helpers -------------------------------------------------------------------------

Scenario default_scenario() {
  Scenario s;
  s.smile = {{1.0 / 12.0, 0.0720, -0.0030, 0.0020},
             {0.25, 0.0745, -0.0045, 0.0026},
             {0.50, 0.0770, -0.0055, 0.0030},
             {1.00, 0.0800, -0.0065, 0.0034}};
  s.forwards = {{"Long EUR forward 9M", 5'000'000.0, 1.0950, 0.75}};
  s.options = {{"Long EUR call 3M 1.10", OptionType::call, 1.10, 0.25, 10'000'000.0},
               {"Short EUR put 3M 1.07", OptionType::put, 1.07, 0.25, -10'000'000.0},
               {"Short straddle call 6M 1.085", OptionType::call, 1.085, 0.50, -4'000'000.0},
               {"Short straddle put 6M 1.085", OptionType::put, 1.085, 0.50, -4'000'000.0},
               {"Long EUR put 1Y 1.05", OptionType::put, 1.05, 1.00, 6'000'000.0}};
  return s;
}

Result<std::vector<double>> parse_returns(const std::string& text) {
  std::vector<double> out;
  std::istringstream in(text);
  std::string line;
  bool seen_first = false;
  std::size_t line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    const auto begin = line.find_first_not_of(" \t\r");
    if (begin == std::string::npos || line[begin] == '#') {
      continue;
    }
    // Take the last comma-separated cell, so "date,return" files work.
    const auto comma = line.find_last_of(',');
    const std::string cell =
        comma == std::string::npos ? line.substr(begin) : line.substr(comma + 1);
    double value = 0.0;
    const bool numeric = parse_number(cell, value);
    if (!numeric) {
      if (!seen_first) {
        seen_first = true;  // a header
        continue;
      }
      return Error{ErrorCode::parse_error,
                   "line " + std::to_string(line_number) + ": '" + cell + "' is not a number"};
    }
    seen_first = true;
    if (!std::isfinite(value) || std::abs(value) > 1.0) {
      return Error{ErrorCode::parse_error,
                   "line " + std::to_string(line_number) +
                       ": a daily log return must be finite and within [-1, 1]"};
    }
    out.push_back(value);
  }
  if (out.size() < 250) {
    return invalid("need at least 250 daily returns, got " + std::to_string(out.size()));
  }
  return out;
}

std::vector<double> synthetic_returns(std::size_t days, std::uint64_t seed) {
  constexpr double omega = 9.0e-7;
  constexpr double alpha = 0.07;
  constexpr double beta = 0.90;
  constexpr std::size_t burn_in = 200;
  mc::NormalStream stream(seed, 0);
  double variance = omega / (1.0 - alpha - beta);
  std::vector<double> out;
  out.reserve(days);
  double z1 = 0.0;
  double z2 = 0.0;
  bool have_second = false;
  for (std::size_t t = 0; t < days + burn_in; ++t) {
    double z = z2;
    if (have_second) {
      have_second = false;
    } else {
      stream.next_pair(z1, z2);
      z = z1;
      have_second = true;
    }
    const double r = std::sqrt(variance) * z;
    variance = omega + alpha * r * r + beta * variance;
    if (t >= burn_in) {
      out.push_back(r);
    }
  }
  return out;
}

Result<double> revalue(const Scenario& scenario, double spot) {
  const auto book = resolve(scenario);
  if (!book) {
    return book.error();
  }
  return book_value(scenario, book.value(), spot);
}

// ---- the analysis ---------------------------------------------------------------------------

Result<Report> analyse(const Scenario& s) {
  Error err{ErrorCode::invalid_argument, ""};
  const auto book_result = resolve(s);
  if (failed(book_result, err)) {
    return err;
  }
  const Book& book = book_result.value();
  if (s.forwards.empty() && s.options.empty()) {
    return invalid("the scenario has no positions");
  }

  Report report;
  report.pair = s.pair;
  report.spot = s.spot;
  report.domestic_rate = s.domestic_rate;
  report.foreign_rate = s.foreign_rate;

  // 1. Positions: value and Greeks
  for (const ForwardInput& f : s.forwards) {
    PositionReport p;
    p.label = f.label;
    p.kind = "forward";
    p.quantity = f.notional;
    p.strike = f.strike;
    p.expiry = f.maturity;
    p.value = forward_value(s, f, s.spot);
    p.greeks = forward_greeks(s, f);
    report.positions.push_back(p);
  }
  for (const ResolvedOption& o : book.options) {
    const auto market = vanilla(book, o, s.spot);
    if (failed(market, err)) {
      return err;
    }
    const auto price = pricing::garman_kohlhagen_price(market.value(), o.input.type);
    const auto greeks = pricing::garman_kohlhagen_greeks(market.value(), o.input.type);
    if (failed(price, err)) {
      return err;
    }
    if (failed(greeks, err)) {
      return err;
    }
    const double q = o.input.quantity;
    const auto& x = greeks.value();
    PositionReport p;
    p.label = o.input.label;
    p.kind = o.input.type == OptionType::call ? "call" : "put";
    p.quantity = q;
    p.strike = o.input.strike;
    p.expiry = o.input.expiry;
    p.volatility = o.volatility;
    p.value = q * price.value();
    p.greeks = {q * x.spot_delta,
                q * x.gamma,
                q * x.vega,
                q * x.vanna,
                q * x.volga,
                q * x.theta,
                q * x.rho_domestic,
                q * x.rho_foreign};
    report.positions.push_back(p);
  }
  for (const PositionReport& p : report.positions) {
    report.total_value += p.value;
    add(report.total_greeks, p.greeks);
  }

  // 2. Model cross-checks: flat ATM volatility (no smile) and calibrated Heston
  const std::size_t first_option = s.forwards.size();
  {
    for (std::size_t i = 0; i < book.options.size(); ++i) {
      ResolvedOption flat = book.options[i];
      const double t = flat.input.expiry;
      const auto v = book.surface.vol(0.0, Years::from_years(t).value());
      if (failed(v, err)) {
        return err;
      }
      flat.volatility = v.value().value();
      const auto m = vanilla(book, flat, s.spot);
      if (failed(m, err)) {
        return err;
      }
      const auto p = pricing::garman_kohlhagen_price(m.value(), flat.input.type);
      if (failed(p, err)) {
        return err;
      }
      report.positions[first_option + i].value_flat_atm = flat.input.quantity * p.value();
    }
  }
  if (s.run_heston) {
    std::vector<calibration::MarketSlice> slices;
    for (const SmileQuote& q : s.smile) {
      slices.push_back({Years::from_years(q.expiry).value(),
                        smile::DeltaQuotes{Volatility::from_decimal(q.atm).value(),
                                           smile::VolSpread::from_decimal(q.risk_reversal).value(),
                                           smile::VolSpread::from_decimal(q.butterfly).value(),
                                           0.25}});
    }
    const calibration::CalibrationMarket market{
        book.spot, book.domestic, book.foreign, smile::DeltaConvention::spot};
    const auto fit =
        calibration::calibrate_heston(market, slices, calibration::CalibrationSettings{});
    if (failed(fit, err)) {
      return err;
    }
    const auto& hp = fit.value().params;
    report.heston = HestonReport{hp.v0(),
                                 hp.kappa(),
                                 hp.theta(),
                                 hp.xi(),
                                 hp.rho(),
                                 fit.value().rms_error,
                                 fit.value().converged};
    for (std::size_t i = 0; i < book.options.size(); ++i) {
      const ResolvedOption& o = book.options[i];
      const models::HestonVanilla hv{book.spot,
                                     Strike::from_value(o.input.strike).value(),
                                     book.domestic,
                                     book.foreign,
                                     Years::from_years(o.input.expiry).value()};
      const auto p = models::heston_price(hv, hp, o.input.type);
      if (failed(p, err)) {
        return err;
      }
      report.positions[first_option + i].value_heston = o.input.quantity * p.value();
    }
  }

  // 3. Spot ladder: full revaluation against the delta-gamma Taylor expansion
  if (s.ladder_points < 2) {
    return invalid("ladder_points must be at least 2");
  }
  for (std::size_t i = 0; i < s.ladder_points; ++i) {
    const double shock = -s.ladder_range + 2.0 * s.ladder_range * static_cast<double>(i) /
                                               static_cast<double>(s.ladder_points - 1);
    const double spot = s.spot * (1.0 + shock);
    const auto v = book_value(s, book, spot);
    if (failed(v, err)) {
      return err;
    }
    const double ds = spot - s.spot;
    report.ladder.push_back(
        {shock,
         spot,
         v.value(),
         v.value() - report.total_value,
         report.total_greeks.delta * ds + 0.5 * report.total_greeks.gamma * ds * ds});
  }

  // 4. Market risk
  if (s.run_market_risk) {
    MarketRiskReport& mr = report.market_risk;
    mr.level = s.var_level;
    std::vector<double> returns = s.returns;
    mr.garch.synthetic = returns.empty();
    if (returns.empty()) {
      returns = synthetic_returns(s.history_days, s.seed);
    }
    const auto fit = timeseries::fit_garch(returns, timeseries::GarchModel::garch);
    if (failed(fit, err)) {
      return err;
    }
    const auto& g = fit.value();
    const double sigma_next = std::sqrt(g.variance_path.back());
    mr.garch.observations = returns.size();
    mr.garch.omega = g.params.omega;
    mr.garch.alpha = g.params.alpha;
    mr.garch.beta = g.params.beta;
    mr.garch.persistence = g.params.persistence();
    mr.garch.sigma_next = sigma_next;
    mr.garch.sigma_unconditional = std::sqrt(g.params.unconditional_variance());
    mr.garch.log_likelihood = g.log_likelihood;
    mr.garch.converged = g.converged;

    const auto portfolio = risk_portfolio(s, book);
    if (failed(portfolio, err)) {
      return err;
    }
    const risk::FxPortfolio& pf = portfolio.value();
    const auto sens = risk::spot_sensitivities(pf);
    if (failed(sens, err)) {
      return err;
    }
    mr.portfolio_delta_units = sens.value().delta;

    const auto residuals = timeseries::standardised_residuals(returns, g.variance_path);
    if (failed(residuals, err)) {
      return err;
    }
    const auto add_row = [&](const std::string& name, const Result<risk::VarEs>& r) -> bool {
      if (failed(r, err)) {
        return false;
      }
      mr.methods.push_back({name, r.value().var, r.value().es});
      return true;
    };
    if (!add_row("Delta-normal", risk::delta_normal_var_es(pf, sigma_next, s.var_level)) ||
        !add_row("Delta-gamma (Cornish-Fisher)",
                 risk::delta_gamma_var_es(pf, sigma_next, s.var_level)) ||
        !add_row("Full revaluation, normal",
                 risk::full_revaluation_normal_var_es(pf, sigma_next, s.var_level)) ||
        !add_row("Historical simulation",
                 risk::historical_revaluation_var_es(pf, returns, s.var_level)) ||
        !add_row(
            "Filtered historical simulation",
            risk::filtered_historical_var_es(pf, residuals.value(), sigma_next, s.var_level))) {
      return err;
    }
    mr.historical_var = mr.methods[3].var;

    std::vector<double> pnl;
    pnl.reserve(returns.size());
    for (const double r : returns) {
      const auto p = risk::pnl_for_log_return(pf, r);
      if (failed(p, err)) {
        return err;
      }
      pnl.push_back(p.value());
    }
    mr.pnl_histogram = histogram(pnl, s.histogram_bins);
  }

  // 5. Counterparty credit risk: one netting set, exposure simulated with the smile ATM volatility
  if (s.run_credit) {
    CreditReport& cr = report.credit;
    cr.hazard_rate = s.hazard_rate;
    cr.recovery = s.recovery;
    cr.paths = s.exposure_paths;
    cr.pfe_level = s.pfe_level;
    const double horizon = longest_maturity(s);
    // Half the longest maturity, kept inside the quoted expiries (the surface does not
    // extrapolate).
    const double mid = std::clamp(0.5 * horizon, s.smile.front().expiry, s.smile.back().expiry);
    const auto atm = book.surface.vol(0.0, Years::from_years(mid).value());
    if (failed(atm, err)) {
      return err;
    }
    cr.exposure_volatility = atm.value().value();
    const credit::ExposureMarket market = exposure_market(book, cr.exposure_volatility);

    credit::NettingSet set;
    std::vector<std::pair<std::string, credit::NettingSet>> singles;
    for (const ForwardInput& f : s.forwards) {
      const credit::FxForwardTrade t{
          f.notional, Strike::from_value(f.strike).value(), Years::from_years(f.maturity).value()};
      set.forwards.push_back(t);
      credit::NettingSet one;
      one.forwards.push_back(t);
      singles.emplace_back(f.label, std::move(one));
    }
    for (const ResolvedOption& o : book.options) {
      const credit::FxOptionTrade t{o.input.type,
                                    Strike::from_value(o.input.strike).value(),
                                    Years::from_years(o.input.expiry).value(),
                                    Volatility::from_decimal(o.volatility).value(),
                                    o.input.quantity};
      set.options.push_back(t);
      credit::NettingSet one;
      one.options.push_back(t);
      singles.emplace_back(o.input.label, std::move(one));
    }
    credit::ExposureSettings settings;
    settings.paths = s.exposure_paths;
    settings.seed = s.seed;
    for (std::size_t i = 1; i <= s.exposure_dates; ++i) {
      settings.dates.push_back(horizon * static_cast<double>(i) /
                               static_cast<double>(s.exposure_dates));
    }
    const credit::CreditInputs inputs{s.hazard_rate, s.recovery, s.domestic_rate};

    const auto values = credit::simulate_netting_set_values(market, set, settings);
    if (failed(values, err)) {
      return err;
    }
    const auto profile = credit::exposure_profile(values.value(), s.pfe_level);
    if (failed(profile, err)) {
      return err;
    }
    cr.dates = profile.value().dates;
    cr.ee = profile.value().ee;
    cr.pfe = profile.value().pfe;
    cr.epe = profile.value().epe;
    const auto cva = credit::cva_estimate(values.value(), inputs);
    const auto from_profile = credit::cva_from_profile(cr.dates, cr.ee, inputs);
    if (failed(cva, err)) {
      return err;
    }
    if (failed(from_profile, err)) {
      return err;
    }
    cr.cva = cva.value().cva;
    cr.cva_standard_error = cva.value().standard_error;
    cr.cva_from_profile = from_profile.value();

    // Standalone CVA of each trade on the same paths: what the counterparty would cost without
    // netting.
    for (const auto& [label, one] : singles) {
      const auto v = credit::simulate_netting_set_values(market, one, settings);
      if (failed(v, err)) {
        return err;
      }
      const auto c = credit::cva_estimate(v.value(), inputs);
      if (failed(c, err)) {
        return err;
      }
      cr.standalone.push_back({label, c.value().cva, c.value().standard_error});
      cr.standalone_sum += c.value().cva;
    }
    cr.netting_benefit = cr.standalone_sum - cr.cva;
  }
  return report;
}

}  // namespace fxval::example
