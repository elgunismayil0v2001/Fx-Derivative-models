#pragma once

#include <string>
#include <string_view>

namespace fxval {

// SHA-256 (FIPS 180-4) of a byte string, as 64 lower-case hexadecimal characters.
//
// Used to fingerprint an input configuration in the run metadata of every output file, so a
// result can be tied to the exact bytes that produced it. It is a fingerprint, not a security
// feature of the library. Checked against the published test vectors and, in the CLI integration
// test, against CMake's own SHA-256 implementation (an independent oracle).
[[nodiscard]] std::string sha256_hex(std::string_view data);

}  // namespace fxval
