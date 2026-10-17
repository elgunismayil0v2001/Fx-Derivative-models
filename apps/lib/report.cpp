#include "report.hpp"

#include <charconv>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>

namespace fxval::app {

namespace {

using ordered_json = nlohmann::ordered_json;

std::string_view type_name(pricing::OptionType type) noexcept {
  return type == pricing::OptionType::call ? "call" : "put";
}

// Shortest decimal text that parses back to exactly the same double. Plain notation (1000000,
// 0.0462556228414579) is preferred because it is what people expect to see in a CSV file; for very
// large or very small values, where plain notation would need many digits, the shortest form with
// an exponent is used instead (1e-300).
std::string number_text(double value) {
  char buffer[64];
  std::to_chars_result result =
      std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
  if (result.ec != std::errc{} || (result.ptr - buffer) > 24) {
    result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  }
  if (result.ec != std::errc{}) {
    return "nan";  // not reachable for finite values; never a silent number
  }
  return {buffer, result.ptr};
}

ordered_json mc_settings_json(const McSettings& settings, bool heston) {
  ordered_json out;
  out["seed"] = settings.config.seed;
  out["paths"] = settings.config.paths;
  if (heston) {
    out["steps"] = settings.config.steps;
  }
  out["threads"] = settings.config.threads;
  out["block_size"] = settings.config.block_size;
  if (heston) {
    out["antithetic"] = settings.reduction.antithetic;
    out["control_variate"] = settings.reduction.forward_control;
    out["pilot_paths"] = settings.reduction.pilot_paths;
  }
  return out;
}

ordered_json heston_json(const models::HestonParams& p) {
  ordered_json out;
  out["v0"] = p.v0();
  out["kappa"] = p.kappa();
  out["theta"] = p.theta();
  out["xi"] = p.xi();
  out["rho"] = p.rho();
  return out;
}

ordered_json model_json(const ModelSpec& model) {
  ordered_json out;
  out["type"] = model_name(model);
  std::visit(
      [&](const auto& spec) {
        using Spec = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<Spec, GarmanKohlhagenSpec>) {
          out["volatility"] = spec.volatility.value();
        } else if constexpr (std::is_same_v<Spec, HestonSemiAnalyticSpec>) {
          out.update(heston_json(spec.params));
        } else if constexpr (std::is_same_v<Spec, GbmMonteCarloSpec>) {
          out["volatility"] = spec.volatility.value();
          out["monte_carlo"] = mc_settings_json(spec.settings, false);
        } else {
          out.update(heston_json(spec.params));
          out["scheme"] = spec.scheme == McScheme::qe ? "qe" : "euler";
          out["monte_carlo"] = mc_settings_json(spec.settings, true);
        }
      },
      model);
  return out;
}

ordered_json greeks_json(const pricing::GarmanKohlhagenGreeks& g) {
  ordered_json out;
  out["spot_delta"] = g.spot_delta;
  out["forward_delta"] = g.forward_delta;
  out["gamma"] = g.gamma;
  out["vega"] = g.vega;
  out["vanna"] = g.vanna;
  out["volga"] = g.volga;
  out["theta"] = g.theta;
  out["rho_domestic"] = g.rho_domestic;
  out["rho_foreign"] = g.rho_foreign;
  return out;
}

ordered_json metadata_json(const RunMetadata& m, std::string_view config_file) {
  ordered_json out;
  out["tool"] = m.tool;
  out["fxval_version"] = m.fxval_version;
  out["git_commit"] = m.git_commit;
  out["compiler"] = m.compiler;
  out["build_type"] = m.build_type;
  out["config_file"] = config_file;
  out["config_sha256"] = m.config_sha256;
  out["timestamp_utc"] = m.timestamp_utc;
  return out;
}

const McSettings* mc_settings_of(const ModelSpec& model) noexcept {
  if (const auto* gbm = std::get_if<GbmMonteCarloSpec>(&model)) {
    return &gbm->settings;
  }
  if (const auto* heston = std::get_if<HestonMonteCarloSpec>(&model)) {
    return &heston->settings;
  }
  return nullptr;
}

constexpr const char* greek_names[] = {"spot_delta",
                                       "forward_delta",
                                       "gamma",
                                       "vega",
                                       "vanna",
                                       "volga",
                                       "theta",
                                       "rho_domestic",
                                       "rho_foreign"};

}  // namespace

std::string csv_field(std::string_view text) {
  if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
    return std::string(text);
  }
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"') {
      out += '"';
    }
    out += c;
  }
  out += '"';
  return out;
}

std::string report_to_json(const RunMetadata& metadata,
                           std::string_view config_file,
                           const PriceConfig& config,
                           const PriceReport& report) {
  ordered_json document;
  document["format_version"] = report_format_version;
  document["metadata"] = metadata_json(metadata, config_file);

  ordered_json inputs;
  inputs["market"] = {{"spot", config.market.spot.value()},
                      {"domestic_rate", config.market.domestic.value()},
                      {"foreign_rate", config.market.foreign.value()}};
  inputs["model"] = model_json(config.model);
  document["inputs"] = std::move(inputs);

  document["summary"] = {{"options", report.options.size()}, {"failed", report.failed()}};

  ordered_json results = ordered_json::array();
  for (const OptionResult& option : report.options) {
    ordered_json row;
    row["id"] = option.id;
    row["type"] = type_name(option.type);
    row["strike"] = option.strike;
    row["expiry_years"] = option.expiry_years;
    row["notional"] = option.notional;
    if (option.ok) {
      row["status"] = "ok";
      row["price"] = option.price;
      row["present_value"] = option.present_value;
      if (option.standard_error) {
        row["standard_error"] = *option.standard_error;
      }
      if (option.greeks) {
        row["greeks"] = greeks_json(*option.greeks);
      }
      if (!option.note.empty()) {
        row["note"] = option.note;
      }
    } else {
      row["status"] = "error";
      row["error"] = option.error;
    }
    results.push_back(std::move(row));
  }
  document["results"] = std::move(results);
  return document.dump(2) + "\n";
}

std::string report_to_csv(const RunMetadata& metadata,
                          std::string_view config_file,
                          const PriceConfig& config,
                          const PriceReport& report) {
  std::string out;
  const auto comment = [&](std::string_view key, std::string_view value) {
    out += "# ";
    out += key;
    out += ": ";
    out += value;
    out += '\n';
  };
  comment("tool", metadata.tool);
  comment("fxval_version", metadata.fxval_version);
  comment("git_commit", metadata.git_commit);
  comment("compiler", metadata.compiler);
  comment("build_type", metadata.build_type);
  comment("config_file", config_file);
  comment("config_sha256", metadata.config_sha256);
  comment("timestamp_utc", metadata.timestamp_utc);
  comment("model", model_name(config.model));
  if (const McSettings* settings = mc_settings_of(config.model)) {
    comment("random_seed", std::to_string(settings->config.seed));
    comment("paths", std::to_string(settings->config.paths));
  }
  comment("prices",
          "domestic currency per one unit of foreign notional; present_value includes notional");

  out += "id,type,strike,expiry_years,notional,status,price,present_value,standard_error";
  for (const char* name : greek_names) {
    out += ',';
    out += name;
  }
  out += ",note,error\n";

  for (const OptionResult& option : report.options) {
    out += csv_field(option.id);
    out += ',';
    out += type_name(option.type);
    out += ',' + number_text(option.strike);
    out += ',' + number_text(option.expiry_years);
    out += ',' + number_text(option.notional);
    out += option.ok ? ",ok" : ",error";
    out += ',' + (option.ok ? number_text(option.price) : std::string{});
    out += ',' + (option.ok ? number_text(option.present_value) : std::string{});
    out += ',' + (option.standard_error ? number_text(*option.standard_error) : std::string{});
    if (option.greeks) {
      const pricing::GarmanKohlhagenGreeks& g = *option.greeks;
      for (const double value : {g.spot_delta,
                                 g.forward_delta,
                                 g.gamma,
                                 g.vega,
                                 g.vanna,
                                 g.volga,
                                 g.theta,
                                 g.rho_domestic,
                                 g.rho_foreign}) {
        out += ',' + number_text(value);
      }
    } else {
      out += std::string(sizeof(greek_names) / sizeof(greek_names[0]), ',');
    }
    out += ',' + csv_field(option.note);
    out += ',' + csv_field(option.error);
    out += '\n';
  }
  return out;
}

}  // namespace fxval::app
