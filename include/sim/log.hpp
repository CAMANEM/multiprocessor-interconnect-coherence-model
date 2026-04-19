#pragma once

#include <ostream>
#include <string>

namespace mp {

/** Verbosity for mp::Log; higher values include more detail when used as the configured ceiling. */
enum class LogLevel : int {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
};

/**
 * Simple static logger for mp_sim: one line per message, simulation time prefix, thread-safe.
 * Default stream is std::cerr so stdout can stay reserved for summary output.
 */
class Log {
public:
  /** Least severe level that is still printed (e.g. Warn => Error and Warn only). */
  static void set_min_level(LogLevel level);

  static LogLevel min_level();

  /** Redirect log output (nullptr resets to std::cerr). */
  static void set_stream(std::ostream* os);

  static bool enabled(LogLevel level);

  static void error(const std::string& msg);
  static void warn(const std::string& msg);
  static void info(const std::string& msg);
  static void debug(const std::string& msg);

private:
  static void write(LogLevel level, const std::string& msg);
};

}  // namespace mp
