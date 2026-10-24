#include "report_writers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

#include "html_page.hpp"

namespace fxval::example {

namespace {

// A minimal JSON builder: objects and arrays are written in order, commas handled by a flag.
class Json {
public:
  Json& begin_object() { return open('{'); }
  Json& end_object() { return close('}'); }
  Json& begin_array() { return open('['); }
  Json& end_array() { return close(']'); }

  Json& key(const std::string& name) {
    separate();
    text(name);
    out_ << ':';
    need_comma_ = false;
    return *this;
  }
  Json& value(double v) {
    separate();
    if (std::isfinite(v)) {
      char buffer[40];
      std::snprintf(buffer, sizeof buffer, "%.12g", v);
      out_ << buffer;
    } else {
      out_ << "null";
    }
    need_comma_ = true;
    return *this;
  }
  Json& value(std::size_t v) {
    separate();
    out_ << v;
    need_comma_ = true;
    return *this;
  }
  Json& value(bool v) {
    separate();
    out_ << (v ? "true" : "false");
    need_comma_ = true;
    return *this;
  }
  Json& value(const std::string& v) {
    separate();
    text(v);
    need_comma_ = true;
    return *this;
  }
  Json& null() {
    separate();
    out_ << "null";
    need_comma_ = true;
    return *this;
  }
  Json& field(const std::string& name, double v) { return key(name).value(v); }
  Json& field(const std::string& name, std::size_t v) { return key(name).value(v); }
  Json& field(const std::string& name, bool v) { return key(name).value(v); }
  Json& field(const std::string& name, const std::string& v) { return key(name).value(v); }
  Json& field(const std::string& name, const std::optional<double>& v) {
    key(name);
    return v ? value(*v) : null();
  }
  Json& numbers(const std::string& name, const std::vector<double>& v) {
    key(name).begin_array();
    for (const double x : v) {
      value(x);
    }
    return end_array();
  }

  [[nodiscard]] std::string str() const { return out_.str(); }

private:
  Json& open(char c) {
    separate();
    out_ << c;
    need_comma_ = false;
    return *this;
  }
  Json& close(char c) {
    out_ << c;
    need_comma_ = true;
    return *this;
  }
  void separate() {
    if (need_comma_) {
      out_ << ',';
    }
    need_comma_ = false;
  }
  void text(const std::string& s) {
    out_ << '"';
    for (const char c : s) {
      switch (c) {
      case '"':
        out_ << "\\\"";
        break;
      case '\\':
        out_ << "\\\\";
        break;
      case '\n':
        out_ << "\\n";
        break;
      case '<':
        out_ << "\\u003c";
        break;  // keeps "</script>" out of the embedded copy
      default:
        if (static_cast<unsigned char>(c) < 0x20U) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(c));
          out_ << buffer;
        } else {
          out_ << c;
        }
      }
    }
    out_ << '"';
  }

  std::ostringstream out_;
  bool need_comma_ = false;
};

void greeks(Json& j, const std::string& name, const GreekSet& g) {
  j.key(name).begin_object();
  j.field("delta", g.delta)
      .field("gamma", g.gamma)
      .field("vega", g.vega)
      .field("vanna", g.vanna)
      .field("volga", g.volga)
      .field("theta", g.theta)
      .field("rho_domestic", g.rho_domestic)
      .field("rho_foreign", g.rho_foreign);
  j.end_object();
}

std::string money(double v) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(0) << v;
  return o.str();
}

}  // namespace

std::string to_json(const Report& r) {
  Json j;
  j.begin_object();
  j.field("pair", r.pair)
      .field("spot", r.spot)
      .field("domestic_rate", r.domestic_rate)
      .field("foreign_rate", r.foreign_rate)
      .field("total_value", r.total_value);
  greeks(j, "total_greeks", r.total_greeks);

  j.key("positions").begin_array();
  for (const PositionReport& p : r.positions) {
    j.begin_object();
    j.field("label", p.label)
        .field("kind", p.kind)
        .field("quantity", p.quantity)
        .field("strike", p.strike)
        .field("expiry", p.expiry)
        .field("volatility", p.volatility)
        .field("value", p.value)
        .field("value_flat_atm", p.value_flat_atm)
        .field("value_heston", p.value_heston);
    greeks(j, "greeks", p.greeks);
    j.end_object();
  }
  j.end_array();

  j.key("heston");
  if (r.heston) {
    j.begin_object();
    j.field("v0", r.heston->v0)
        .field("kappa", r.heston->kappa)
        .field("theta", r.heston->theta)
        .field("xi", r.heston->xi)
        .field("rho", r.heston->rho)
        .field("rms_vol_error", r.heston->rms_vol_error)
        .field("converged", r.heston->converged);
    j.end_object();
  } else {
    j.null();
  }

  j.key("ladder").begin_array();
  for (const LadderPoint& l : r.ladder) {
    j.begin_object();
    j.field("shock", l.shock)
        .field("spot", l.spot)
        .field("value", l.value)
        .field("pnl", l.pnl)
        .field("pnl_taylor", l.pnl_taylor);
    j.end_object();
  }
  j.end_array();

  const MarketRiskReport& m = r.market_risk;
  j.key("market_risk").begin_object();
  j.field("level", m.level)
      .field("portfolio_delta_units", m.portfolio_delta_units)
      .field("historical_var", m.historical_var);
  j.key("garch").begin_object();
  j.field("synthetic", m.garch.synthetic)
      .field("observations", m.garch.observations)
      .field("omega", m.garch.omega)
      .field("alpha", m.garch.alpha)
      .field("beta", m.garch.beta)
      .field("persistence", m.garch.persistence)
      .field("sigma_next", m.garch.sigma_next)
      .field("sigma_unconditional", m.garch.sigma_unconditional)
      .field("log_likelihood", m.garch.log_likelihood)
      .field("converged", m.garch.converged);
  j.end_object();
  j.key("methods").begin_array();
  for (const VarRow& v : m.methods) {
    j.begin_object();
    j.field("method", v.method).field("var", v.var).field("es", v.es);
    j.end_object();
  }
  j.end_array();
  j.key("pnl_histogram").begin_object();
  j.field("lower", m.pnl_histogram.lower).field("upper", m.pnl_histogram.upper);
  j.key("counts").begin_array();
  for (const std::size_t c : m.pnl_histogram.counts) {
    j.value(c);
  }
  j.end_array();
  j.end_object();
  j.end_object();

  const CreditReport& c = r.credit;
  j.key("credit").begin_object();
  j.field("hazard_rate", c.hazard_rate)
      .field("recovery", c.recovery)
      .field("paths", c.paths)
      .field("exposure_volatility", c.exposure_volatility)
      .field("pfe_level", c.pfe_level)
      .field("epe", c.epe)
      .field("cva", c.cva)
      .field("cva_standard_error", c.cva_standard_error)
      .field("cva_from_profile", c.cva_from_profile)
      .field("standalone_sum", c.standalone_sum)
      .field("netting_benefit", c.netting_benefit);
  j.numbers("dates", c.dates).numbers("ee", c.ee).numbers("pfe", c.pfe);
  j.key("standalone").begin_array();
  for (const StandaloneCva& s : c.standalone) {
    j.begin_object();
    j.field("label", s.label).field("cva", s.cva).field("standard_error", s.standard_error);
    j.end_object();
  }
  j.end_array();
  j.end_object();

  j.end_object();
  return j.str();
}

std::string to_text(const Report& r) {
  std::ostringstream o;
  o << std::fixed;
  o << "Portfolio " << r.pair << "  spot " << std::setprecision(4) << r.spot << "  r_d "
    << std::setprecision(2) << 100.0 * r.domestic_rate << "%  r_f " << 100.0 * r.foreign_rate
    << "%  (domestic currency amounts)\n\n";
  o << std::left << std::setw(32) << "Position" << std::right << std::setw(14) << "Quantity"
    << std::setw(9) << "Vol %" << std::setw(16) << "Value" << '\n';
  for (const PositionReport& p : r.positions) {
    o << std::left << std::setw(32) << p.label << std::right << std::setw(14) << money(p.quantity)
      << std::setw(9) << std::setprecision(2) << 100.0 * p.volatility << std::setw(16)
      << money(p.value) << '\n';
  }
  o << std::left << std::setw(55) << "Total value" << std::right << std::setw(16)
    << money(r.total_value) << "\n\n";

  const GreekSet& g = r.total_greeks;
  o << "Greeks (total)\n"
    << "  delta " << money(g.delta) << " EUR   cash delta " << money(g.delta * r.spot)
    << "   gamma per 1% move " << money(0.5 * g.gamma * r.spot * r.spot * 1e-4) << '\n'
    << "  vega per vol point " << money(g.vega / 100.0) << "   theta per day "
    << money(g.theta / 365.0) << "   rho_d per bp " << money(g.rho_domestic / 1e4) << '\n';
  if (r.heston) {
    o << "  Heston calibrated to the surface: rms vol error " << std::setprecision(3)
      << 100.0 * r.heston->rms_vol_error << " vol points\n";
  }

  const MarketRiskReport& m = r.market_risk;
  o << "\nMarket risk, 1 day, " << std::setprecision(1) << 100.0 * m.level
    << "% (loss as a positive number)\n"
    << "  GARCH next-day volatility " << std::setprecision(3) << 100.0 * m.garch.sigma_next << "% ("
    << (m.garch.synthetic ? "synthetic history" : "supplied history") << ", "
    << m.garch.observations << " days)\n";
  for (const VarRow& v : m.methods) {
    o << "  " << std::left << std::setw(32) << v.method << std::right << "VaR " << std::setw(10)
      << money(v.var) << "   ES " << std::setw(10) << money(v.es) << '\n';
  }

  const CreditReport& c = r.credit;
  o << "\nCounterparty credit risk (hazard " << std::setprecision(1) << 100.0 * c.hazard_rate
    << "%, recovery " << 100.0 * c.recovery << "%, " << c.paths << " paths)\n"
    << "  CVA netted " << money(c.cva) << " +/- " << money(c.cva_standard_error)
    << "   sum of standalone " << money(c.standalone_sum) << "   netting benefit "
    << money(c.netting_benefit) << "\n  EPE " << money(c.epe) << "   peak EE "
    << money(c.ee.empty() ? 0.0 : *std::max_element(c.ee.begin(), c.ee.end())) << '\n';
  return o.str();
}

std::string to_html(const Report& r) {
  std::string page = html_page_template();
  const std::string marker = "/*REPORT_JSON*/null";
  const auto at = page.find(marker);
  if (at != std::string::npos) {
    page.replace(at, marker.size(), to_json(r));
  }
  return page;
}

}  // namespace fxval::example
