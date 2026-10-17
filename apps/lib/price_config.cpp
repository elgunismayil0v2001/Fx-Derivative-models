#include "price_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <set>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace fxval::app {

namespace {

using json = nlohmann::json;

// Internal control flow only: thrown inside this file, converted to a Result before returning
// from parse_price_config. Nothing here escapes to a caller.
struct ConfigError {
  ErrorCode code;
  std::string message;
};

[[noreturn]] void fail(std::string_view path, std::string_view what) {
  std::string message(path);
  if (!path.empty()) {
    message += ": ";
  }
  message += what;
  throw ConfigError{ErrorCode::invalid_argument, std::move(message)};
}

std::string child(std::string_view path, std::string_view key) {
  std::string out(path);
  if (!path.empty()) {
    out += '.';
  }
  out += key;
  return out;
}

std::string indexed(std::string_view path, std::size_t index) {
  return std::string(path) + "[" + std::to_string(index) + "]";
}

std::string_view type_name(const json& value) noexcept {
  switch (value.type()) {
  case json::value_t::null:
    return "null";
  case json::value_t::object:
    return "an object";
  case json::value_t::array:
    return "an array";
  case json::value_t::string:
    return "a string";
  case json::value_t::boolean:
    return "a boolean";
  case json::value_t::number_integer:
  case json::value_t::number_unsigned:
  case json::value_t::number_float:
    return "a number";
  default:
    return "an unsupported value";
  }
}

// ---- Detects repeated keys and excessive nesting while checking the syntax -------------------
// nlohmann::json keeps the last of two equal keys without a word. For a validation tool that is a
// hole (two "volatility" entries, one silently winning), so a first pass with a SAX handler looks
// for repeats. The same pass bounds the nesting depth.
class StructureChecker final : public json::json_sax_t {
public:
  static constexpr std::size_t max_depth = 16;

  [[nodiscard]] const std::string& duplicate_key() const noexcept { return duplicate_; }
  [[nodiscard]] bool too_deep() const noexcept { return too_deep_; }
  [[nodiscard]] const std::string& syntax_error() const noexcept { return syntax_error_; }

  bool null() override { return true; }
  bool boolean(bool /*value*/) override { return true; }
  bool number_integer(number_integer_t /*value*/) override { return true; }
  bool number_unsigned(number_unsigned_t /*value*/) override { return true; }
  bool number_float(number_float_t /*value*/, const string_t& /*text*/) override { return true; }
  bool string(string_t& /*value*/) override { return true; }
  bool binary(binary_t& /*value*/) override { return true; }
  bool start_object(std::size_t /*size*/) override {
    if (++depth_ > max_depth) {
      too_deep_ = true;
      return false;
    }
    keys_.emplace_back();
    return true;
  }
  bool key(string_t& name) override {
    if (!keys_.back().insert(name).second) {
      duplicate_ = name;
      return false;
    }
    return true;
  }
  bool end_object() override {
    keys_.pop_back();
    --depth_;
    return true;
  }
  bool start_array(std::size_t /*size*/) override {
    if (++depth_ > max_depth) {
      too_deep_ = true;
      return false;
    }
    return true;
  }
  bool end_array() override {
    --depth_;
    return true;
  }
  bool parse_error(std::size_t /*position*/,
                   const std::string& /*token*/,
                   const json::exception& error) override {
    syntax_error_ = error.what();
    return false;
  }

private:
  std::size_t depth_ = 0;
  std::vector<std::set<std::string>> keys_;
  std::string duplicate_;
  std::string syntax_error_;
  bool too_deep_ = false;
};

json parse_document(std::string_view text) {
  StructureChecker checker;
  if (!json::sax_parse(text, &checker)) {
    if (!checker.duplicate_key().empty()) {
      throw ConfigError{ErrorCode::parse_error,
                        "duplicate key \"" + checker.duplicate_key() +
                            "\": a JSON object must not repeat a key"};
    }
    if (checker.too_deep()) {
      throw ConfigError{ErrorCode::parse_error, "the document is nested more than 16 levels deep"};
    }
    throw ConfigError{ErrorCode::parse_error, "not valid JSON: " + checker.syntax_error()};
  }
  json document = json::parse(text);
  if (!document.is_object()) {
    throw ConfigError{ErrorCode::parse_error,
                      std::string("the top level must be an object, not ") +
                          std::string(type_name(document))};
  }
  return document;
}

// ---- Typed readers ---------------------------------------------------------------------------

const json& require(const json& object, std::string_view path, std::string_view key) {
  const auto found = object.find(std::string(key));
  if (found == object.end()) {
    fail(child(path, key), "missing required field");
  }
  return *found;
}

const json& require_object(const json& object, std::string_view path, std::string_view key) {
  const json& value = require(object, path, key);
  if (!value.is_object()) {
    fail(child(path, key), std::string("expected an object, got ") + std::string(type_name(value)));
  }
  return value;
}

double number(const json& object, std::string_view path, std::string_view key) {
  const json& value = require(object, path, key);
  if (!value.is_number()) {
    fail(child(path, key), std::string("expected a number, got ") + std::string(type_name(value)));
  }
  return value.get<double>();
}

std::string text(const json& object, std::string_view path, std::string_view key) {
  const json& value = require(object, path, key);
  if (!value.is_string()) {
    fail(child(path, key), std::string("expected a string, got ") + std::string(type_name(value)));
  }
  return value.get<std::string>();
}

std::uint64_t unsigned_integer(const json& value, std::string_view path) {
  if (value.is_number_unsigned()) {
    return value.get<std::uint64_t>();
  }
  if (value.is_number_integer()) {
    fail(path, "must not be negative, got " + std::to_string(value.get<std::int64_t>()));
  }
  fail(path,
       std::string("expected a non-negative integer, got ") + std::string(type_name(value)) +
           (value.is_number() ? " that is not an integer" : ""));
}

std::uint64_t required_unsigned(const json& object, std::string_view path, std::string_view key) {
  return unsigned_integer(require(object, path, key), child(path, key));
}

std::uint64_t optional_unsigned(const json& object,
                                std::string_view path,
                                std::string_view key,
                                std::uint64_t fallback) {
  const auto found = object.find(std::string(key));
  return found == object.end() ? fallback : unsigned_integer(*found, child(path, key));
}

bool optional_bool(const json& object, std::string_view path, std::string_view key, bool fallback) {
  const auto found = object.find(std::string(key));
  if (found == object.end()) {
    return fallback;
  }
  if (!found->is_boolean()) {
    fail(child(path, key),
         std::string("expected true or false, got ") + std::string(type_name(*found)));
  }
  return found->get<bool>();
}

void allow_only(const json& object,
                std::string_view path,
                std::initializer_list<std::string_view> allowed) {
  for (const auto& item : object.items()) {
    bool known = false;
    for (const std::string_view name : allowed) {
      known = known || item.key() == name;
    }
    if (!known) {
      std::ostringstream message;
      message << "unknown field (allowed here:";
      bool first = true;
      for (const std::string_view name : allowed) {
        message << (first ? " " : ", ") << name;
        first = false;
      }
      message << ")";
      fail(child(path, item.key()), message.str());
    }
  }
}

// Wraps a library Result<T>: the library's own message is kept and prefixed with the field path.
template <class T>
T unwrap(Result<T> result, std::string_view path) {
  if (!result) {
    fail(path, result.error().message);
  }
  return std::move(result).value();
}

// ---- Sections --------------------------------------------------------------------------------

MarketSpec read_market(const json& document) {
  constexpr std::string_view path = "market";
  const json& market = require_object(document, "", path);
  allow_only(market, path, {"spot", "domestic_rate", "foreign_rate"});
  return MarketSpec{unwrap(Spot::from_value(number(market, path, "spot")), child(path, "spot")),
                    unwrap(Rate::from_decimal(number(market, path, "domestic_rate")),
                           child(path, "domestic_rate")),
                    unwrap(Rate::from_decimal(number(market, path, "foreign_rate")),
                           child(path, "foreign_rate"))};
}

models::HestonParams read_heston(const json& model, std::string_view path) {
  const double v0 = number(model, path, "v0");
  const double kappa = number(model, path, "kappa");
  const double theta = number(model, path, "theta");
  const double xi = number(model, path, "xi");
  const double rho = number(model, path, "rho");
  return unwrap(models::HestonParams::create(v0, kappa, theta, xi, rho), path);
}

McSettings read_monte_carlo(const json& model, std::string_view model_path, bool heston) {
  const std::string path = child(model_path, "monte_carlo");
  const json& block = require_object(model, model_path, "monte_carlo");
  if (heston) {
    allow_only(block,
               path,
               {"seed",
                "paths",
                "steps",
                "threads",
                "block_size",
                "antithetic",
                "control_variate",
                "pilot_paths"});
  } else {
    allow_only(block, path, {"seed", "paths", "threads", "block_size"});
  }
  McSettings settings{};
  // The seed is required: a result that nobody can reproduce is not a result (ADR 0001, V-07).
  settings.config.seed = required_unsigned(block, path, "seed");
  settings.config.paths = static_cast<std::size_t>(required_unsigned(block, path, "paths"));
  settings.config.steps =
      heston ? static_cast<std::size_t>(required_unsigned(block, path, "steps")) : std::size_t{1};
  settings.config.threads = static_cast<unsigned>(
      std::min<std::uint64_t>(optional_unsigned(block, path, "threads", 1), mc::max_threads + 1U));
  settings.config.block_size = static_cast<std::size_t>(
      optional_unsigned(block, path, "block_size", mc::McConfig{}.block_size));
  if (heston) {
    settings.reduction.antithetic = optional_bool(block, path, "antithetic", false);
    settings.reduction.forward_control = optional_bool(block, path, "control_variate", false);
    settings.reduction.pilot_paths = static_cast<std::size_t>(
        optional_unsigned(block, path, "pilot_paths", mc::McReduction{}.pilot_paths));
  }
  Error problem{ErrorCode::invalid_argument, {}};
  if (!mc::detail::validate(settings.config, problem)) {
    fail(path, problem.message);
  }
  return settings;
}

ModelSpec read_model(const json& document) {
  constexpr std::string_view path = "model";
  const json& model = require_object(document, "", path);
  const std::string type = text(model, path, "type");
  if (type == "garman_kohlhagen") {
    allow_only(model, path, {"type", "volatility"});
    return GarmanKohlhagenSpec{unwrap(Volatility::from_decimal(number(model, path, "volatility")),
                                      child(path, "volatility"))};
  }
  if (type == "heston_semi_analytic") {
    allow_only(model, path, {"type", "v0", "kappa", "theta", "xi", "rho"});
    return HestonSemiAnalyticSpec{read_heston(model, path)};
  }
  if (type == "gbm_monte_carlo") {
    allow_only(model, path, {"type", "volatility", "monte_carlo"});
    return GbmMonteCarloSpec{unwrap(Volatility::from_decimal(number(model, path, "volatility")),
                                    child(path, "volatility")),
                             read_monte_carlo(model, path, false)};
  }
  if (type == "heston_monte_carlo") {
    allow_only(model, path, {"type", "v0", "kappa", "theta", "xi", "rho", "scheme", "monte_carlo"});
    const models::HestonParams params = read_heston(model, path);
    const std::string scheme_name = text(model, path, "scheme");
    McScheme scheme{};
    if (scheme_name == "euler") {
      scheme = McScheme::euler;
    } else if (scheme_name == "qe") {
      scheme = McScheme::qe;
      if (!(params.xi() > 0.0)) {
        fail(child(path, "xi"), "the QE scheme needs xi > 0 (use scheme \"euler\" when xi = 0)");
      }
    } else {
      fail(child(path, "scheme"), "unknown scheme \"" + scheme_name + "\" (allowed: euler, qe)");
    }
    return HestonMonteCarloSpec{params, scheme, read_monte_carlo(model, path, true)};
  }
  fail(child(path, "type"),
       "unknown model \"" + type +
           "\" (allowed: garman_kohlhagen, heston_semi_analytic, gbm_monte_carlo, "
           "heston_monte_carlo)");
}

bool valid_id(const std::string& id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  const auto alnum = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  };
  if (!alnum(id.front())) {
    return false;
  }
  return std::ranges::all_of(
      id, [&](char c) { return alnum(c) || c == '.' || c == '_' || c == ':' || c == '-'; });
}

std::vector<OptionSpec> read_options(const json& document) {
  constexpr std::string_view path = "options";
  const json& list = require(document, "", path);
  if (!list.is_array()) {
    fail(path, std::string("expected an array, got ") + std::string(type_name(list)));
  }
  if (list.empty()) {
    fail(path, "must contain at least one option");
  }
  if (list.size() > max_options) {
    fail(path,
         "too many options (" + std::to_string(list.size()) + ", at most " +
             std::to_string(max_options) + ")");
  }
  std::vector<OptionSpec> options;
  options.reserve(list.size());
  std::set<std::string> seen;
  for (std::size_t i = 0; i < list.size(); ++i) {
    const std::string here = indexed(path, i);
    const json& entry = list[i];
    if (!entry.is_object()) {
      fail(here, std::string("expected an object, got ") + std::string(type_name(entry)));
    }
    allow_only(entry, here, {"id", "type", "strike", "expiry_years", "notional"});
    std::string id = text(entry, here, "id");
    if (!valid_id(id)) {
      fail(child(here, "id"),
           "must be 1 to 64 characters from A-Z a-z 0-9 . _ : - and start with a letter or digit");
    }
    if (!seen.insert(id).second) {
      fail(child(here, "id"), "duplicate id \"" + id + "\"");
    }
    const std::string kind = text(entry, here, "type");
    if (kind != "call" && kind != "put") {
      fail(child(here, "type"), "unknown option type \"" + kind + "\" (allowed: call, put)");
    }
    const double expiry = number(entry, here, "expiry_years");
    if (!(expiry > 0.0)) {
      fail(child(here, "expiry_years"), "must be greater than 0");
    }
    const double notional = entry.contains("notional") ? number(entry, here, "notional") : 1.0;
    options.push_back(
        OptionSpec{std::move(id),
                   kind == "call" ? pricing::OptionType::call : pricing::OptionType::put,
                   unwrap(Strike::from_value(number(entry, here, "strike")), child(here, "strike")),
                   unwrap(Years::from_years(expiry), child(here, "expiry_years")),
                   unwrap(Notional::from_value(notional), child(here, "notional"))});
  }
  return options;
}

PriceConfig read_config(const json& document) {
  allow_only(document, "", {"format_version", "market", "model", "options"});
  const json& version = require(document, "", "format_version");
  if (!version.is_number_integer() || version.get<std::int64_t>() != price_config_format_version) {
    fail("format_version",
         "unsupported value " + version.dump() + " (this program reads version " +
             std::to_string(price_config_format_version) + ")");
  }
  MarketSpec market = read_market(document);
  ModelSpec model = read_model(document);
  std::vector<OptionSpec> options = read_options(document);
  return PriceConfig{market, model, std::move(options)};
}

}  // namespace

std::string_view model_name(const ModelSpec& model) noexcept {
  switch (model.index()) {
  case 0:
    return "garman_kohlhagen";
  case 1:
    return "heston_semi_analytic";
  case 2:
    return "gbm_monte_carlo";
  default:
    return "heston_monte_carlo";
  }
}

Result<PriceConfig> parse_price_config(std::string_view input) {
  try {
    const json document = parse_document(input);
    return read_config(document);
  } catch (const ConfigError& error) {
    return Error{error.code, error.message};
  } catch (const std::exception& error) {
    return Error{ErrorCode::parse_error,
                 std::string("unexpected problem while reading the configuration: ") +
                     error.what()};
  }
}

}  // namespace fxval::app
