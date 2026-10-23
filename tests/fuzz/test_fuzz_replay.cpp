// Replay of the fuzz targets in every build (including the sanitizer builds).
//
// For each target: (1) every file of its seed corpus and of its regression directory, (2) a fixed
// number of seeded mutations of the seeds (bit flips, byte changes, insertions, deletions,
// truncation, splices, and overwriting 8 bytes with an "interesting" double such as 0, -0, NaN,
// infinity or the largest finite value), and (3) purely random inputs. The generator is seeded, so
// a run is reproducible and a failure prints the input as hex to be saved under
// tests/fuzz/regressions/<target>/ where it is replayed from then on.
//
// This is not coverage-guided fuzzing and finds less than libFuzzer would; it is the part that can
// run in every build and keeps the targets and the invariants themselves tested. The libFuzzer
// entry points
// (-DFXVAL_BUILD_FUZZERS=ON) are the real thing and run in the nightly CI job.

// GCC 13 at -O3 reports a false "stringop-overflow" when a std::vector<uint8_t> of unknown size is
// copied (the copy of a seed in mutate()); the sizes are checked by the vector itself.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "fuzz_targets.hpp"
#ifdef FXVAL_FUZZ_HAVE_CONFIG
#include "fuzz_config_target.hpp"
#endif

#ifndef FXVAL_FUZZ_DIR
#error "FXVAL_FUZZ_DIR must be defined by the build system"
#endif

namespace {

namespace fs = std::filesystem;
using Input = std::vector<std::uint8_t>;
using Check = std::function<std::string(const std::uint8_t*, std::size_t)>;

std::vector<Input> load(const fs::path& dir) {
  std::vector<Input> out;
  if (!fs::is_directory(dir)) {
    return out;
  }
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.is_regular_file()) {
      files.push_back(e.path());
    }
  }
  std::sort(files.begin(), files.end());  // the order of a directory listing is not specified
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    out.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  return out;
}

std::string hex(const Input& in) {
  std::ostringstream s;
  for (const auto b : in) {
    s << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
  }
  return s.str();
}

void put_double(Input& in, std::size_t at, double x) {
  if (in.size() < at + 8) {
    in.resize(at + 8, 0);
  }
  // Byte by byte instead of one memcpy: GCC 14 reports a false stringop-overflow for the memcpy
  // after the resize above (the vector cannot be empty here, but the compiler cannot see that).
  std::array<std::uint8_t, sizeof x> raw{};
  std::memcpy(raw.data(), &x, sizeof x);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    in[at + i] = raw[i];
  }
}

Input mutate(const std::vector<Input>& seeds, std::mt19937_64& rng) {
  static const double interesting[] = {0.0,
                                       -0.0,
                                       1.0,
                                       -1.0,
                                       0.5,
                                       std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity(),
                                       std::numeric_limits<double>::max(),
                                       std::numeric_limits<double>::denorm_min(),
                                       std::numeric_limits<double>::min(),
                                       1e-12,
                                       1e12};
  Input in = seeds[rng() % seeds.size()];
  const int edits = 1 + static_cast<int>(rng() % 4);
  for (int e = 0; e < edits; ++e) {
    if (in.empty()) {
      in.push_back(static_cast<std::uint8_t>(rng()));
      continue;
    }
    const std::size_t at = rng() % in.size();
    switch (rng() % 8) {
    case 0:
      in[at] = static_cast<std::uint8_t>(in[at] ^ (1U << (rng() % 8)));
      break;
    case 1:
      in[at] = static_cast<std::uint8_t>(rng());
      break;
    case 2:
      in.insert(in.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(rng()));
      break;
    case 3:
      in.erase(in.begin() + static_cast<std::ptrdiff_t>(at));
      break;
    case 4:
      in.resize(at);
      break;
    case 5: {
      const Input& other = seeds[rng() % seeds.size()];
      if (!other.empty()) {
        const std::size_t from = rng() % other.size();
        for (std::size_t i = from; i < other.size(); ++i) {
          in.push_back(other[i]);
        }
      }
      break;
    }
    case 6:
      put_double(in,
                 1 + (at / 8) * 8 % (in.size()),
                 interesting[rng() % (sizeof interesting / sizeof(double))]);
      break;
    default: {
      const std::size_t len = 1 + rng() % 8;
      for (std::size_t i = 0; i < len && at + i < in.size(); ++i) {
        in[at + i] = in[at];
      }
      break;
    }
    }
  }
  return in;
}

// FXVAL_FUZZ_SCALE multiplies every iteration count (default 1); use it for
// one-off deeper runs, e.g. FXVAL_FUZZ_SCALE=20.
int scale() {
  const char* v = std::getenv("FXVAL_FUZZ_SCALE");
  const int n = v != nullptr ? std::atoi(v) : 1;
  return n > 0 ? n : 1;
}

// std::hash is implementation-defined; a fixed FNV-1a keeps the replay identical
// on libstdc++ and libc++.
std::uint64_t stable_hash(const std::string& text) {
  std::uint64_t h = 1469598103934665603ULL;
  for (const char c : text) {
    h = (h ^ static_cast<std::uint8_t>(c)) * 1099511628211ULL;
  }
  return h;
}

void replay(const std::string& target,
            const Check& check,
            int base_mutations,
            int base_random_inputs,
            double min_reached_share = 0.0) {
  const int mutations = base_mutations * scale();
  const int random_inputs = base_random_inputs * scale();
  const fs::path base = fs::path(FXVAL_FUZZ_DIR);
  std::vector<Input> seeds = load(base / "corpus" / target);
  ASSERT_FALSE(seeds.empty()) << "no seed corpus for " << target;
  const std::vector<Input> regressions = load(base / "regressions" / target);
  int violations = 0;
  const std::size_t reached_before = fxval::fuzz::reached();
  const auto run = [&](const Input& in, const char* origin) {
    const std::string v = check(in.data(), in.size());
    if (!v.empty() && violations++ < 3) {
      ADD_FAILURE() << target << " (" << origin << "): " << v
                    << "\ninput (hex, save under tests/fuzz/regressions/" << target
                    << "/): " << hex(in);
    }
  };
  for (const auto& in : seeds) {
    run(in, "seed");
  }
  for (const auto& in : regressions) {
    run(in, "regression");
  }
  std::mt19937_64 rng(0x5eed0000ULL + stable_hash(target) % 1000);
  for (int i = 0; i < mutations; ++i) {
    run(mutate(seeds, rng), "mutation");
  }
  for (int i = 0; i < random_inputs; ++i) {
    Input in(rng() % 160);
    for (auto& b : in) {
      b = static_cast<std::uint8_t>(rng());
    }
    run(in, "random");
  }
  const int total = static_cast<int>(seeds.size() + regressions.size()) + mutations + random_inputs;
  const std::size_t reached = fxval::fuzz::reached() - reached_before;
  std::cout << "fuzz replay " << target << ": " << total << " inputs";
  if (min_reached_share > 0.0) {
    std::cout << ", " << reached << " got past validation";
  }
  std::cout << "\n";
  ::testing::Test::RecordProperty("inputs", total);
  EXPECT_EQ(violations, 0) << target;
  EXPECT_GE(static_cast<double>(reached), min_reached_share * static_cast<double>(total))
      << target << ": too few inputs got past validation, the target is nearly vacuous";
}

TEST(FuzzReplay, Vanilla) {
  replay("vanilla", fxval::fuzz::check_vanilla, 20000, 5000, 0.10);
}
TEST(FuzzReplay, Barrier) {
  replay("barrier", fxval::fuzz::check_barrier, 20000, 5000, 0.10);
}
TEST(FuzzReplay, Delta) {
  replay("delta", fxval::fuzz::check_delta, 20000, 5000, 0.10);
}
TEST(FuzzReplay, Risk) {
  replay("risk", fxval::fuzz::check_risk, 20000, 5000);
}
TEST(FuzzReplay, Credit) {
  replay("credit", fxval::fuzz::check_credit, 3000, 1000, 0.05);
}
TEST(FuzzReplay, Sha256) {
  replay("sha256", fxval::fuzz::check_sha256, 5000, 1000);
}
#ifdef FXVAL_FUZZ_HAVE_CONFIG
TEST(FuzzReplay, PriceConfig) {
  replay("price_config", fxval::fuzz::check_price_config, 20000, 5000);
}
#endif

}  // namespace
