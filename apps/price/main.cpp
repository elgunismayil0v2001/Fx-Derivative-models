// fxval-price: price European FX options from a JSON configuration.
// All behaviour lives in apps/lib/cli.cpp so that tests can call it without starting a process.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"

int main(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  fxval::app::CliEnvironment environment;
  if (const char* epoch = std::getenv("SOURCE_DATE_EPOCH")) {  // NOLINT(concurrency-mt-unsafe)
    environment.epoch_seconds = fxval::app::parse_epoch_seconds(epoch);
    if (!environment.epoch_seconds) {
      std::cerr << "fxval-price: error: SOURCE_DATE_EPOCH must be whole seconds (digits only, "
                   "at most 253402300799), got '"
                << epoch << "'\n";
      return static_cast<int>(fxval::app::ExitCode::usage);
    }
  }
  return fxval::app::run_price_cli(args, std::cin, std::cout, std::cerr, environment);
}
