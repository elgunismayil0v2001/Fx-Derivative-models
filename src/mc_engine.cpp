#include "fxval/mc/engine.hpp"

#include <cmath>
#include <sstream>
#include <thread>

namespace fxval::mc::detail {

bool validate(const McConfig& config, Error& problem) {
  const auto bad = [&problem](const char* what, const char* rule) {
    std::ostringstream message;
    message << "McConfig." << what << " must be " << rule;
    problem = Error{ErrorCode::invalid_argument, message.str()};
    return false;
  };
  if (config.paths < 2 || config.paths > max_paths) {
    return bad("paths", "in [2, 2e9]: a standard error needs at least two paths");
  }
  if (config.steps < 1 || config.steps > max_steps) {
    return bad("steps", "in [1, 1e6]");
  }
  if (config.threads > max_threads) {
    return bad("threads", "at most 256 (0 means hardware concurrency)");
  }
  if (config.block_size < 1) {
    return bad("block_size", "at least 1");
  }
  return true;
}

unsigned resolve_threads(unsigned requested, std::size_t blocks) noexcept {
  unsigned wanted = requested;
  if (wanted == 0) {
    wanted = std::max(1U, std::thread::hardware_concurrency());
  }
  const std::size_t capped = std::min<std::size_t>(wanted, std::max<std::size_t>(blocks, 1));
  return static_cast<unsigned>(capped);
}

}  // namespace fxval::mc::detail
