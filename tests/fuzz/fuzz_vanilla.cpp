// libFuzzer entry point: build with -DFXVAL_BUILD_FUZZERS=ON under clang (see docs/fuzzing.md).
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fuzz_targets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string violation = fxval::fuzz::check_vanilla(data, size);
  if (!violation.empty()) {
    std::cerr << "invariant violated: " << violation << "\n";
    std::abort();
  }
  return 0;
}
