#include "fxval/core/contracts.hpp"

#include <cstdio>
#include <cstdlib>

namespace fxval::detail {

// GCOVR_EXCL_START
// Exercised by death tests, which run in a child process that aborts before it can write
// coverage data. The behaviour is tested; only the measurement is impossible.
void contract_violation(const char* expression, const char* file, int line) noexcept {
  std::fprintf(stderr, "fxval: contract violated: %s (%s:%d)\n", expression, file, line);
  std::abort();
}
// GCOVR_EXCL_STOP

}  // namespace fxval::detail
