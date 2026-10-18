// Example: build smiles from the synthetic scenarios in configs/scenarios/smile_scenarios.csv, run
// the butterfly and calendar checks, and write three CSV files for python/plot_smiles.py.
//
//   fxval-example-smile configs/scenarios/smile_scenarios.csv docs/results/smile_data
//
// Uses only the public library API. Empty cells mean "undefined here" (never NaN).

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "fxval/smile/arbitrage_checks.hpp"
#include "fxval/smile/smile_slice.hpp"
#include "fxval/smile/vol_surface.hpp"

namespace {

using namespace fxval;
using namespace fxval::smile;

struct Row {
  std::string scenario;
  std::string tenor;
  double expiry = 0.0;
  double spot = 0.0;
  double rd = 0.0;
  double rf = 0.0;
  double atm = 0.0;
  double rr = 0.0;
  double bf = 0.0;
};

template <class T>
T must_or_exit(Result<T> result, const std::string& what) {
  if (!result) {
    std::cerr << what << ": " << to_string(result.error().code) << ": " << result.error().message
              << '\n';
    std::exit(1);
  }
  return std::move(result).value();
}

std::vector<Row> read_rows(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "cannot open " << path << '\n';
    std::exit(2);
  }
  std::vector<Row> rows;
  std::string line;
  bool header = false;
  while (std::getline(in, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (!header) {
      header = true;
      continue;
    }
    std::stringstream ss(line);
    Row r{};
    std::string cell;
    std::getline(ss, r.scenario, ',');
    std::getline(ss, r.tenor, ',');
    double* fields[] = {&r.expiry, &r.spot, &r.rd, &r.rf, &r.atm, &r.rr, &r.bf};
    for (double* f : fields) {
      std::getline(ss, cell, ',');
      *f = std::stod(cell);
    }
    rows.push_back(r);
  }
  return rows;
}

SmileSlice build_slice(const Row& r) {
  const SmileMarket market{must_or_exit(Spot::from_value(r.spot), "spot"),
                           must_or_exit(Rate::from_decimal(r.rd), "rd"),
                           must_or_exit(Rate::from_decimal(r.rf), "rf"),
                           must_or_exit(Years::from_years(r.expiry), "expiry")};
  const DeltaQuotes quotes{must_or_exit(Volatility::from_decimal(r.atm), "atm"),
                           must_or_exit(VolSpread::from_decimal(r.rr), "rr"),
                           must_or_exit(VolSpread::from_decimal(r.bf), "bf"),
                           0.25};
  return must_or_exit(SmileSlice::from_quotes(market, quotes, DeltaConvention::spot),
                      r.scenario + " " + r.tenor);
}

void put(std::ostream& out, double v) {
  out << std::setprecision(10) << v;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <scenarios.csv> <output-directory>\n";
    return 1;
  }
  const std::vector<Row> rows = read_rows(argv[1]);
  const std::filesystem::path dir = argv[2];
  std::filesystem::create_directories(dir);
  std::ofstream curves(dir / "smile_curves.csv");
  std::ofstream pivots(dir / "smile_pivots.csv");
  std::ofstream checks(dir / "smile_checks.csv");
  curves << "scenario,tenor,expiry_years,x,strike,vol,density_factor\n";
  pivots << "scenario,tenor,strike_put,strike_atm,strike_call,vol_put,vol_atm,vol_call\n";
  checks << "scenario,tenor,kind,grid_points,violations,undefined_points,min_value,"
            "innermost_x,innermost_abs_delta\n";

  std::map<std::string, std::vector<Row>> by_scenario;
  for (const Row& r : rows) {
    by_scenario[r.scenario].push_back(r);
  }
  for (const auto& [name, group] : by_scenario) {
    std::vector<SmileSlice> slices;
    for (const Row& r : group) {
      slices.push_back(build_slice(r));
    }
    const VolSurface surface = must_or_exit(VolSurface::create(slices), name);
    const SurfaceReport report = must_or_exit(check_surface(surface), name);

    for (std::size_t i = 0; i < slices.size(); ++i) {
      const SmileSlice& s = slices[i];
      pivots << name << ',' << group[i].tenor << ',';
      put(pivots, s.pivot_strikes().put), pivots << ',';
      put(pivots, s.pivot_strikes().atm), pivots << ',';
      put(pivots, s.pivot_strikes().call), pivots << ',';
      put(pivots, s.pivot_volatilities().put.value()), pivots << ',';
      put(pivots, s.pivot_volatilities().atm.value()), pivots << ',';
      put(pivots, s.pivot_volatilities().call.value()), pivots << '\n';

      for (int j = 0; j <= 160; ++j) {
        const double x = -4.0 + 0.05 * j;
        const double k = x * s.atm_scale();
        curves << name << ',' << group[i].tenor << ',' << group[i].expiry << ',' << x << ',';
        put(curves, s.forward() * std::exp(k)), curves << ',';
        const double vol = s.point(k).vol;
        if (vol > 0.0) {
          put(curves, vol), curves << ',';
          put(curves, must_or_exit(density_factor(s, k), name));
        } else {
          curves << ',';
        }
        curves << '\n';
      }

      const ButterflyReport& b = report.butterfly[i];
      checks << name << ',' << group[i].tenor << ",butterfly," << b.grid_points << ','
             << b.violations << ',' << b.non_positive_vol_points << ',';
      put(checks, b.min_density_factor), checks << ',';
      if (b.innermost) {
        put(checks, b.innermost->standardised), checks << ',';
        put(checks, b.innermost->abs_delta);
      } else {
        checks << ',';
      }
      checks << '\n';
      if (i > 0) {
        const CalendarReport& c = report.calendar[i - 1];
        checks << name << ',' << group[i].tenor << ",calendar," << c.grid_points << ','
               << c.violations << ',' << c.undefined_points << ',';
        put(checks, c.worst_relative_change), checks << ',';
        if (c.innermost) {
          put(checks, c.innermost->standardised), checks << ',';
          put(checks, c.innermost->abs_delta);
        } else {
          checks << ',';
        }
        checks << '\n';
      }
    }
  }
  return 0;
}
