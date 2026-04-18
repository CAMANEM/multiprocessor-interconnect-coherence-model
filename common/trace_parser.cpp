#include "trace_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mp {
namespace {

bool parse_uint64(const std::string& token, int base, std::uint64_t& out) {
  try {
    std::size_t idx = 0;
    unsigned long long v = std::stoull(token, &idx, base);
    if (idx != token.size()) {
      return false;
    }
    out = static_cast<std::uint64_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_int(const std::string& token, int& out) {
  try {
    std::size_t idx = 0;
    long long v = std::stoll(token, &idx, 10);
    if (idx != token.size()) {
      return false;
    }
    out = static_cast<int>(v);
    return true;
  } catch (...) {
    return false;
  }
}

std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

}  // namespace

bool TraceFile::load(const std::string& path, std::string& error_out) {
  entries_.clear();
  std::ifstream in(path);
  if (!in) {
    error_out = "cannot open trace file: " + path;
    return false;
  }

  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream iss(line);
    std::string tick_tok;
    std::string pe_tok;
    std::string op_tok;
    std::string addr_tok;
    std::string size_tok;
    if (!(iss >> tick_tok >> pe_tok >> op_tok >> addr_tok)) {
      error_out = "line " + std::to_string(line_no) + ": expected: tick pe_id R|W address [size]";
      return false;
    }
    (void)(iss >> size_tok);

    std::uint64_t tick = 0;
    if (!parse_uint64(tick_tok, 10, tick)) {
      error_out = "line " + std::to_string(line_no) + ": bad tick";
      return false;
    }

    int pe_id = 0;
    if (!parse_int(pe_tok, pe_id)) {
      error_out = "line " + std::to_string(line_no) + ": bad pe_id";
      return false;
    }

    MemoryOperation op = MemoryOperation::Read;
    if (op_tok == "R" || op_tok == "r") {
      op = MemoryOperation::Read;
    } else if (op_tok == "W" || op_tok == "w") {
      op = MemoryOperation::Write;
    } else {
      error_out = "line " + std::to_string(line_no) + ": op must be R or W";
      return false;
    }

    std::uint64_t address = 0;
    try {
      std::size_t idx = 0;
      unsigned long long v = std::stoull(addr_tok, &idx, 0);
      if (idx != addr_tok.size()) {
        error_out = "line " + std::to_string(line_no) + ": bad address";
        return false;
      }
      address = static_cast<std::uint64_t>(v);
    } catch (...) {
      error_out = "line " + std::to_string(line_no) + ": bad address";
      return false;
    }

    std::uint32_t size = 4;
    if (!size_tok.empty()) {
      std::uint64_t sz64 = 0;
      if (!parse_uint64(size_tok, 10, sz64) || sz64 == 0 || sz64 > 0xFFFFFFFFull) {
        error_out = "line " + std::to_string(line_no) + ": bad size";
        return false;
      }
      size = static_cast<std::uint32_t>(sz64);
    }

    entries_.push_back(TraceEntry{tick, pe_id, op, address, size});
  }

  std::sort(entries_.begin(), entries_.end(), [](const TraceEntry& a, const TraceEntry& b) {
    if (a.tick != b.tick) {
      return a.tick < b.tick;
    }
    if (a.pe_id != b.pe_id) {
      return a.pe_id < b.pe_id;
    }
    return a.address < b.address;
  });

  return true;
}

std::vector<TraceEntry> TraceFile::entries_for_pe(int pe_id) const {
  std::vector<TraceEntry> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) {
    if (e.pe_id == pe_id) {
      out.push_back(e);
    }
  }
  return out;
}

}  // namespace mp
