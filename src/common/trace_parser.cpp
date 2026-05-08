#include "trace_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mp {
namespace {

/**
 * Parses an unsigned integer token in the given radix.
 *
 * @param token string to parse (must be consumed entirely)
 * @param base numeric base (e.g. 10 or 16)
 * @param out parsed value written on success
 * @return true on successful parse
 */
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

/**
 * Parses a signed decimal integer from a full token string.
 *
 * @param token string to parse
 * @param out parsed value
 * @return true on successful parse
 */
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

/**
 * Trims leading and trailing whitespace (operates on a copy).
 *
 * @param s string to trim
 * @return trimmed copy
 */
std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

/**
 * Infers the byte size of a token value.
 *
 * @param value_tok value token from the trace line
 * @return inferred byte size, or 4 as fallback
 */
std::uint32_t infer_size_from_value(const std::string& value_tok) {
  if (value_tok.empty()) return 4;

  // For strings
  if (value_tok.size() >= 2 && value_tok.front() == '"' && value_tok.back() == '"') {
    return static_cast<std::uint32_t>(value_tok.size() - 2);  // strip surrounding quotes
  }

  // For chars
  if (value_tok.size() >= 3 && value_tok.front() == '\'' && value_tok.back() == '\'') {
    return 1;
  }

  // For integers
  try {
    std::size_t idx = 0;
    unsigned long long v = std::stoull(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      if (v <= 0xFFULL)         return 1;
      if (v <= 0xFFFFFFFFULL)   return 4;
      return 8;
    }
  } catch (...) {}

  // For signed integer
  try {
    std::size_t idx = 0;
    long long v = std::stoll(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      if (v >= -128 && v <= 127)              return 1;
      if (v >= -2147483648LL && v <= 2147483647LL) return 4;
      return 8;
    }
  } catch (...) {}

  // For others: Uses string logic
  return static_cast<std::uint32_t>(value_tok.size());
}

}  // namespace

/**
 * Loads from disk, validates each line, and sorts entries_ at the end.
 */
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
    if (!(iss >> tick_tok >> pe_tok >> op_tok >> addr_tok)) {
      error_out = "line " + std::to_string(line_no) + ": expected: tick pe_id R|W address [size] [value]";
      return false;
    }

    // Read optional remaining tokens: could be just size, just value, or both
    std::string tok5, tok6, tok7;
    (void)(iss >> tok5);
    (void)(iss >> tok6);
    (void)(iss >> tok7);

    // Separate size_tok and value_tok from tok5/tok6/tok7.
    std::string size_tok;
    std::string value_tok;
    std::string priority_tok;

    auto is_plain_number = [](const std::string& t) -> bool {
      if (t.empty()) return false;
      if (t.front() == '"' || t.front() == '\'') return false;
      try {
        std::size_t idx = 0;
        unsigned long long v = std::stoull(t, &idx, 0);
        return idx == t.size() && v > 0;
      } catch (...) { return false; }
    };

    if (!tok5.empty()) {
      if (is_plain_number(tok5) && tok6.empty()) {
        size_tok = tok5;
      } else if (is_plain_number(tok5) && !tok6.empty()) {
        size_tok  = tok5;
        value_tok = tok6;
        if (!tok7.empty()) {
          priority_tok = tok7;
        }
      } else {
        value_tok = tok5;
        if (!tok6.empty()) {
          priority_tok = tok6;
        }
      }
    }

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

    // Uses explicit size or infers it.
    std::uint32_t size = 4;
    if (!size_tok.empty()) {
      std::uint64_t sz64 = 0;
      if (!parse_uint64(size_tok, 10, sz64) || sz64 == 0 || sz64 > 0xFFFFFFFFull) {
        error_out = "line " + std::to_string(line_no) + ": bad size";
        return false;
      }
      size = static_cast<std::uint32_t>(sz64);
    } else if (!value_tok.empty()) {
      // Infer size from the value token
      size = infer_size_from_value(value_tok);
      // In case of bad infer
      if (size == 0) size = 4;
    }

    /* Parses the priority token */
    int priority = 0;
    if (!priority_tok.empty()) {
      if (priority_tok == "1") {
        priority = 1;
      } else if (priority_tok != "0") {
        error_out = "line " + std::to_string(line_no) + ": invalid priority. Priority must be 0 or 1."; 
        return false;
      }
    }

    entries_.push_back(TraceEntry{tick, pe_id, op, address, size, value_tok, priority});
  }

  std::sort(entries_.begin(), entries_.end(), [](const TraceEntry& a, const TraceEntry& b) {
    if (a.tick != b.tick) {
      return a.tick < b.tick;
    }
    if (a.priority != b.priority){
      /* Higher priority first */
      return a.priority > b.priority;
    }
    if (a.pe_id != b.pe_id) {
      return a.pe_id < b.pe_id;
    }
    return a.address < b.address;
  });

  return true;
}

/**
 * Returns a copy of entries whose pe_id matches the argument.
 */
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
