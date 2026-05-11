#include <iostream>
#include <fstream>
#include <string>
#include <systemc>
#include "event_log.hpp"
#include "log.hpp"
#include "monitor.hpp"
#include "top.hpp"

namespace {

std::string ascii_lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --trace <file.trace>"
               " [--protocol msi|firefly]"
               " [--csv <file.csv>]"
               " [--log-level error|warn|info|debug]"
               " [-v]\n"
            << "\n"
            << "  --csv  Genera o agrega una fila de estadisticas al archivo CSV.\n"
            << "         Si el archivo no existe lo crea con cabecera.\n"
            << "         Si ya existe agrega la fila sin repetir la cabecera.\n"
            << "         Util para comparar multiples runs en una sola tabla.\n"
            << "\n"
            << "  Ejemplo para tabla comparativa completa:\n"
            << "    mp_sim --trace pc.trace    --protocol msi     --csv resultados.csv\n"
            << "    mp_sim --trace pc.trace    --protocol firefly --csv resultados.csv\n"
            << "    mp_sim --trace migra.trace --protocol msi     --csv resultados.csv\n"
            << "    mp_sim --trace migra.trace --protocol firefly --csv resultados.csv\n";
}

// Extrae el nombre base del archivo sin extension ni ruta
// ej. "src/traces/producer_consumer.trace" -> "producer_consumer"
std::string basename_no_ext(const std::string& path) {
  std::size_t slash = path.find_last_of("/\\");
  std::string name  = (slash == std::string::npos) ? path : path.substr(slash + 1);
  std::size_t dot   = name.rfind('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

// Devuelve true si el archivo existe y tiene contenido
bool file_exists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

bool parse_args(int argc, char* argv[],
                std::string& trace_path,
                mp::CoherenceProtocolKind& proto,
                mp::LogLevel& log_level,
                std::string& csv_path) {
  trace_path.clear();
  csv_path.clear();
  proto     = mp::CoherenceProtocolKind::Msi;
  log_level = mp::LogLevel::Warn;

  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--trace" && i + 1 < argc) {
      trace_path = argv[++i];
    } else if (a == "--protocol" && i + 1 < argc) {
      const std::string p(argv[++i]);
      if      (p == "msi"    || p == "MSI")                        proto = mp::CoherenceProtocolKind::Msi;
      else if (p == "firefly"|| p == "Firefly" || p == "FIREFLY")  proto = mp::CoherenceProtocolKind::Firefly;
      else { std::cerr << "Unknown protocol: " << p << "\n"; return false; }
    } else if (a == "--csv" && i + 1 < argc) {
      csv_path = argv[++i];
    } else if (a == "--log-level" && i + 1 < argc) {
      const std::string pl = ascii_lower(argv[++i]);
      if      (pl == "error") log_level = mp::LogLevel::Error;
      else if (pl == "warn")  log_level = mp::LogLevel::Warn;
      else if (pl == "info")  log_level = mp::LogLevel::Info;
      else if (pl == "debug") log_level = mp::LogLevel::Debug;
      else { std::cerr << "Unknown log level: " << pl << "\n"; return false; }
    } else if (a == "-v") {
      log_level = mp::LogLevel::Info;
    } else {
      return false;
    }
  }
  return !trace_path.empty();
}

}  // namespace

int sc_main(int argc, char* argv[]) {
  std::string trace_path;
  std::string csv_path;
  mp::CoherenceProtocolKind proto     = mp::CoherenceProtocolKind::Msi;
  mp::LogLevel              log_level = mp::LogLevel::Warn;

  if (!parse_args(argc, argv, trace_path, proto, log_level, csv_path)) {
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

  // Correr simulacion
  sc_core::sc_start();

  // Imprimir log ordenado cronologicamente
  mp::EventLog::dump(std::cout);
  std::cout << "\n";

  // Resumen en consola
  top.monitor().dump_summary_line(std::cout);
  std::cout << "\n";

  if (!csv_path.empty()) {
    const bool exists = file_exists(csv_path);

    std::ofstream csv(csv_path, std::ios::app);
    if (!csv) {
      std::cerr << "No se pudo abrir CSV: " << csv_path << "\n";
      return 4;
    }

    if (!exists) {
      mp::Monitor::dump_csv_header(csv);
    }

    const std::string trace_name = basename_no_ext(trace_path);
    const std::string proto_str  = (proto == mp::CoherenceProtocolKind::Msi)
                                   ? "msi" : "firefly";
    const double sim_end_ns = sc_core::sc_time_stamp().to_double();

    top.monitor().dump_csv_row(csv, trace_name, proto_str, sim_end_ns);

    std::cout << "CSV actualizado: " << csv_path << "\n";
  }

  return 0;
}