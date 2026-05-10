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
    if (idx != token.size()) return false;
    out = static_cast<std::uint64_t>(v);
    return true;
  } catch (...) { return false; }
}

bool parse_int(const std::string& token, int& out) {
  try {
    std::size_t idx = 0;
    long long v = std::stoll(token, &idx, 10);
    if (idx != token.size()) return false;
    out = static_cast<int>(v);
    return true;
  } catch (...) { return false; }
}

std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::uint32_t infer_size_from_value(const std::string& value_tok) {
  if (value_tok.empty()) return 4;
  if (value_tok.size() >= 2 &&
      value_tok.front() == '"' && value_tok.back() == '"')
    return static_cast<std::uint32_t>(value_tok.size() - 2);
  if (value_tok.size() >= 3 &&
      value_tok.front() == '\'' && value_tok.back() == '\'')
    return 1;
  try {
    std::size_t idx = 0;
    unsigned long long v = std::stoull(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      if (v <= 0xFFULL)       return 1;
      if (v <= 0xFFFFFFFFULL) return 4;
      return 8;
    }
  } catch (...) {}
  try {
    std::size_t idx = 0;
    long long v = std::stoll(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      if (v >= -128 && v <= 127)                   return 1;
      if (v >= -2147483648LL && v <= 2147483647LL) return 4;
      return 8;
    }
  } catch (...) {}
  return static_cast<std::uint32_t>(value_tok.size());
}

// ---------------------------------------------------------------
//  Convierte token de operacion a enum.
//  Para agregar nuevas operaciones: agregar caso aqui.
// ---------------------------------------------------------------
bool op_from_string(const std::string& tok, MemoryOperation& op) {
  // Convertir a mayusculas para comparacion case-insensitive
  std::string upper = tok;
  for (char& c : upper)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

  if (upper == "R")   { op = MemoryOperation::Read;  return true; }
  if (upper == "W")   { op = MemoryOperation::Write; return true; }
  if (upper == "ADD") { op = MemoryOperation::Add;   return true; }
  if (upper == "SUB") { op = MemoryOperation::Sub;   return true; }
  return false;
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
    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    std::string tick_tok, pe_tok, op_tok, addr_tok;
    if (!(iss >> tick_tok >> pe_tok >> op_tok >> addr_tok)) {
      error_out = "line " + std::to_string(line_no) +
                  ": expected: tick pe_id R|W|ADD|SUB address [size] [value]";
      return false;
    }

    std::string tok5, tok6;
    (void)(iss >> tok5);
    (void)(iss >> tok6);

    // Separar size_tok y value_tok
    std::string size_tok, value_tok;
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
      } else {
        value_tok = tok5;
      }
    }

    // Parsear tick
    std::uint64_t tick = 0;
    if (!parse_uint64(tick_tok, 10, tick)) {
      error_out = "line " + std::to_string(line_no) + ": bad tick";
      return false;
    }

    // Parsear pe_id
    int pe_id = 0;
    if (!parse_int(pe_tok, pe_id)) {
      error_out = "line " + std::to_string(line_no) + ": bad pe_id";
      return false;
    }
    if (pe_id < 0 || pe_id >= kMaxPes) {
      error_out = "line " + std::to_string(line_no) + ": pe_id out of range";
      return false;
    }

    // Parsear operacion (R, W, ADD, SUB)
    MemoryOperation op;
    if (!op_from_string(op_tok, op)) {
      error_out = "line " + std::to_string(line_no) +
                  ": op must be R, W, ADD or SUB (got: " + op_tok + ")";
      return false;
    }

    // Parsear address
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

    // Parsear size (explicito o inferido del valor)
    std::uint32_t size = 4;
    if (!size_tok.empty()) {
      std::uint64_t sz64 = 0;
      if (!parse_uint64(size_tok, 10, sz64) || sz64 == 0 || sz64 > 0xFFFFFFFFull) {
        error_out = "line " + std::to_string(line_no) + ": bad size";
        return false;
      }
      size = static_cast<std::uint32_t>(sz64);
    } else if (!value_tok.empty()) {
      size = infer_size_from_value(value_tok);
      if (size == 0) size = 4;
    }

    // ADD y SUB requieren un valor (el operando)
    // Si no se especifico, se usa "0" como delta neutro con advertencia
    if ((op == MemoryOperation::Add || op == MemoryOperation::Sub) &&
        value_tok.empty()) {
      value_tok = "0";
    }

    entries_.push_back(TraceEntry{tick, pe_id, op, address, size, value_tok});
  }

  // Deterministic order for per-PE extraction.
  std::stable_sort(entries_.begin(), entries_.end(),
    [](const TraceEntry& a, const TraceEntry& b) {
      if (a.tick != b.tick) return a.tick < b.tick;
      return a.pe_id < b.pe_id;
    });

  return true;
}

std::vector<TraceEntry> TraceFile::entries_for_pe(int pe_id) const {
  std::vector<TraceEntry> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_)
    if (e.pe_id == pe_id) out.push_back(e);
  return out;
}

}  // namespace mp