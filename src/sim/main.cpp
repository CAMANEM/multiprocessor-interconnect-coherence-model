#include <iostream>
#include <string>

#include <systemc>

#include "log.hpp"
#include "top.hpp"

namespace {

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}


/**
 * Prints command-line syntax to stderr.
 *
 * @param argv0 executable name (argv[0])
 */
void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --trace <file.trace> [--protocol msi|firefly] [--log-level error|warn|info|debug] "
               "[-v]\n";
}

/**
 * Parses simulation command-line arguments.
 *
 * @param argc argument count
 * @param argv arguments (argv[0] is the program name)
 * @param trace_path output: path to trace file
 * @param proto output: selected coherence protocol (MSI or Firefly)
 * @param log_level output: least severe log level to print (-v sets Info)
 * @return true if arguments are valid and trace_path is non-empty
 */
bool parse_args(int argc, char* argv[], std::string& trace_path, mp::CoherenceProtocolKind& proto,
                mp::LogLevel& log_level) {
  trace_path.clear();
  proto = mp::CoherenceProtocolKind::Msi;
  log_level = mp::LogLevel::Warn;

  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--trace" && i + 1 < argc) {
      trace_path = argv[++i];
    } else if (a == "--protocol" && i + 1 < argc) {
      const std::string p(argv[++i]);
      if (p == "msi" || p == "MSI") {
        proto = mp::CoherenceProtocolKind::Msi;
      } else if (p == "firefly" || p == "Firefly" || p == "FIREFLY") {
        proto = mp::CoherenceProtocolKind::Firefly;
      } else {
        std::cerr << "Unknown protocol: " << p << "\n";
        return false;
      }
    } else if (a == "--log-level" && i + 1 < argc) {
      const std::string pl = ascii_lower(argv[++i]);
      if (pl == "error") {
        log_level = mp::LogLevel::Error;
      } else if (pl == "warn") {
        log_level = mp::LogLevel::Warn;
      } else if (pl == "info") {
        log_level = mp::LogLevel::Info;
      } else if (pl == "debug") {
        log_level = mp::LogLevel::Debug;
      } else {
        std::cerr << "Unknown log level: " << pl << "\n";
        return false;
      }
    } else if (a == "-v") {
      log_level = mp::LogLevel::Info;
    } else {
      return false;
    }
  }

  return !trace_path.empty();
}

}  // namespace

/**
 * SystemC entry: validates CLI, loads trace, elaborates Top, runs until end of simulation.
 *
 * @param argc argument count
 * @param argv command-line arguments
 * @return process exit code (0 success, 2 bad usage, 3 trace error)
 */
int sc_main(int argc, char* argv[]) {
  std::string trace_path;
  mp::CoherenceProtocolKind proto = mp::CoherenceProtocolKind::Msi;
  mp::LogLevel log_level = mp::LogLevel::Warn;
  if (!parse_args(argc, argv, trace_path, proto, log_level)) {
    usage(argv[0]);
    return 2;
  }

  mp::Log::set_min_level(log_level);

  mp::TraceFile trace;
  std::string err;
  if (!trace.load(trace_path, err)) {
    std::cerr << err << "\n";
    return 3;
  }

  mp::Top top("top", trace, proto);
  sc_core::sc_start();
  top.monitor().dump_summary_line(std::cout);
  std::cout << "\n";
  return 0;
}
