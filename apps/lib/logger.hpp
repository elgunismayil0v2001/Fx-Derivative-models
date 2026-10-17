#pragma once

#include <ostream>
#include <string_view>

namespace fxval::app {

// A minimal levelled logger for the command-line programs. Library code does not log: it returns
// errors. Programs log a few lines per run (never inside a hot loop), to the stream they are given,
// so tests can capture them. spdlog was considered and left out: see docs/adr/0007.
enum class LogLevel { quiet = 0, error = 1, warning = 2, info = 3, debug = 4 };

class Logger {
public:
  Logger(std::ostream& stream, LogLevel level) noexcept : stream_(&stream), level_(level) {}

  [[nodiscard]] LogLevel level() const noexcept { return level_; }

  void error(std::string_view message) const { write(LogLevel::error, "error", message); }
  void warning(std::string_view message) const { write(LogLevel::warning, "warning", message); }
  void info(std::string_view message) const { write(LogLevel::info, "info", message); }
  void debug(std::string_view message) const { write(LogLevel::debug, "debug", message); }

private:
  void write(LogLevel level, std::string_view name, std::string_view message) const {
    if (static_cast<int>(level) <= static_cast<int>(level_)) {
      *stream_ << "fxval-price: " << name << ": " << message << '\n';
    }
  }

  std::ostream* stream_;
  LogLevel level_;
};

}  // namespace fxval::app
