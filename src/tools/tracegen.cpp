#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {

/**
 * Prints trace generator usage to stderr.
 *
 * @param argv0 executable name
 */
void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " --workload pc|migratory --output <file.trace>\n";
}

/**
 * Writes a synthetic producer-consumer trace to os (shared line 0x8000).
 *
 * @param os open output stream
 */
void write_producer_consumer(std::ostream& os) {
  os << "# CE4302 trace v1\n";
  os << "# Workload: producer-consumer (shared line ~0x8000)\n";
  std::uint64_t t = 0;
  const std::uint64_t addr = 0x8000;

  for (int round = 0; round < 32; ++round) {
    os << t++ << " 0 W " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 1 R " << std::hex << "0x" << addr << std::dec << " 4\n";
  }
}

/**
 * Writes a synthetic migratory trace to os (line 0x9000, round-robin PE0..PE3).
 *
 * @param os open output stream
 */
void write_migratory(std::ostream& os) {
  os << "# CE4302 trace v1\n";
  os << "# Workload: migratory pattern (same line handed across PEs)\n";
  std::uint64_t t = 0;
  const std::uint64_t addr = 0x9000;

  for (int lap = 0; lap < 24; ++lap) {
    os << t++ << " 0 R " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 0 W " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 1 R " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 1 W " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 2 R " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 2 W " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 3 R " << std::hex << "0x" << addr << std::dec << " 4\n";
    os << t++ << " 3 W " << std::hex << "0x" << addr << std::dec << " 4\n";
  }
}

}  // namespace

/**
 * Parses --workload and --output, writes the selected trace file.
 *
 * @param argc argument count
 * @param argv arguments
 * @return 0 on success, 2 on bad usage or unknown workload, 3 if output cannot be opened
 */
int main(int argc, char* argv[]) {
  std::string workload;
  std::string output;

  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--workload" && i + 1 < argc) {
      workload = argv[++i];
    } else if (a == "--output" && i + 1 < argc) {
      output = argv[++i];
    } else {
      print_usage(argv[0]);
      return 2;
    }
  }

  if (workload.empty() || output.empty()) {
    print_usage(argv[0]);
    return 2;
  }

  std::ofstream out(output, std::ios::binary);
  if (!out) {
    std::cerr << "cannot open output: " << output << "\n";
    return 3;
  }

  if (workload == "pc" || workload == "producer-consumer") {
    write_producer_consumer(out);
  } else if (workload == "migratory" || workload == "migrate") {
    write_migratory(out);
  } else {
    std::cerr << "unknown workload: " << workload << "\n";
    print_usage(argv[0]);
    return 2;
  }

  return 0;
}
