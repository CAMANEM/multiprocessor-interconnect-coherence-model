#include "log.hpp"

#include <iostream>
#include <mutex>

#include <systemc>

namespace mp {

namespace {

std::mutex g_log_mutex;
LogLevel g_min_level = LogLevel::Warn;
std::ostream* g_out = &std::cerr;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Debug:
      return "DEBUG";
  }
  return "?";
}

}  // namespace

void Log::set_min_level(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_min_level = level;
}

LogLevel Log::min_level() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  return g_min_level;
}

void Log::set_stream(std::ostream* os) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_out = os ? os : &std::cerr;
}

bool Log::enabled(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  return static_cast<int>(level) <= static_cast<int>(g_min_level);
}

void Log::error(const std::string& msg) {
  write(LogLevel::Error, msg);
}

void Log::warn(const std::string& msg) {
  write(LogLevel::Warn, msg);
}

void Log::info(const std::string& msg) {
  write(LogLevel::Info, msg);
}

void Log::debug(const std::string& msg) {
  write(LogLevel::Debug, msg);
}

void Log::write(LogLevel level, const std::string& msg) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (static_cast<int>(level) > static_cast<int>(g_min_level)) {
    return;
  }
  std::ostream& out = g_out ? *g_out : std::cerr;
  out << sc_core::sc_time_stamp().to_string() << ' ' << level_name(level) << ' ' << msg << '\n';
}

}  // namespace mp
