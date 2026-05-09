/**
 * tracegen.cpp  —  Generador y compilador de traces CE4302 v2
 * ============================================================
 *
 * Modos de uso:
 *   1. Workloads predefinidos:
 *      tracegen --workload pc        --output out.trace
 *      tracegen --workload migratory --output out.trace
 *      tracegen --workload add_sub   --output out.trace
 *
 *   2. Compilar pseudocodigo .txt a .trace:
 *      tracegen --compile prog.txt --output out.trace
 *      tracegen --compile prog.txt              (out = prog.trace)
 *
 * Sintaxis del lenguaje .txt:
 * -----------------------------------------------------------------------
 *   # comentario
 *   workload "nombre"
 *   description "texto"
 *   pes 4
 *
 *   pe(0): write 0x8000 100
 *   pe(1): read  0x8000
 *   pe(2): add   0x8000 10
 *   pe(3): sub   0x9000 5
 *
 *   parallel {
 *       pe(0): write 0x8000 1
 *       pe(2): write 0x9000 2
 *   }
 *
 *   repeat 8 {
 *       pe(0): add 0x8000 $i
 *       pe(1): read 0x8000
 *   }
 *
 *   barrier
 * -----------------------------------------------------------------------
 *
 * Para agregar nuevas operaciones (ej. MUL):
 *   1. Agregar "mul" a is_modifying() si modifica memoria
 *   2. Agregar el caso en emit_op()
 *   3. Agregar el token en Lexer::next_token()
 */

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------
//  Latencias del sistema (sincronizar con top.cpp)
// ---------------------------------------------------------------
static constexpr std::uint64_t L1_LATENCY   =  1;
static constexpr std::uint64_t IC_LATENCY   =  2;
static constexpr std::uint64_t MEM_LATENCY  = 40;
static constexpr std::uint64_t MISS_LATENCY = L1_LATENCY + IC_LATENCY + MEM_LATENCY;

// ---------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------
static std::string to_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static bool is_modifying(const std::string& op) {
  const std::string lo = to_lower(op);
  return lo == "w" || lo == "write" || lo == "add" || lo == "sub";
void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --workload pc|migratory|eviction --output <file.trace>\n";
}

// ---------------------------------------------------------------
//  DependencyTracker
// ---------------------------------------------------------------
class DependencyTracker {
public:
  void write(std::uint64_t addr, std::uint64_t tick) {
    last_write_[addr] = tick;
  }

  std::uint64_t next_safe(std::uint64_t addr) const {
    auto it = last_write_.find(addr);
    if (it == last_write_.end()) return 0;
    return it->second + MISS_LATENCY;
  }

  DependencyTracker clone() const {
    return *this;
  }

private:
  std::unordered_map<std::uint64_t, std::uint64_t> last_write_;
};

// ---------------------------------------------------------------
//  TraceEntry: una operacion resuelta con tick calculado
// ---------------------------------------------------------------
struct TraceEntry {
  std::uint64_t tick;
  int           pe_id;
  std::string   op;       // R, W, ADD, SUB
  std::uint64_t addr;
  int           size;
  std::string   value;    // vacio para lecturas
  std::string   comment;
};

// ---------------------------------------------------------------
//  TraceBuilder en C++
// ---------------------------------------------------------------
class TraceBuilder {
public:
  std::string name;
  std::string description;
  int         num_pes{4};

  // Emite una operacion con causalidad automatica
  void add_op(int pe_id, const std::string& op,
              std::uint64_t addr, int size,
              const std::string& value,
              const std::string& comment = "") {
    const std::uint64_t t_pe  = pe_clocks_.count(pe_id) ? pe_clocks_[pe_id] : 0;
    const std::uint64_t t_dep = dep_.next_safe(addr);
    const std::uint64_t tick  = std::max(t_pe, t_dep);

    entries_.push_back(TraceEntry{tick, pe_id, op, addr, size, value, comment});

    // ADD/SUB hacen read+write: 2*MISS
    const std::uint64_t cost = (to_lower(op) == "add" || to_lower(op) == "sub")
                               ? 2 * MISS_LATENCY : MISS_LATENCY;
    pe_clocks_[pe_id] = tick + cost;

    // ADD/SUB: registrar dependencia en tick+MISS_LATENCY (cuando la
    // escritura de vuelta llega a memoria), no en tick (inicio del RMW).
    // Asi el siguiente PE espera a que el RMW complete antes de leer.
    if (is_modifying(op)) {
      const std::uint64_t dep_tick = (to_lower(op) == "add" || to_lower(op) == "sub")
                                     ? tick + MISS_LATENCY
                                     : tick;
      dep_.write(addr, dep_tick);
    }
  }

  // Agrega un comentario (no genera entrada en el trace)
  void add_comment(const std::string& text) {
    entries_.push_back(TraceEntry{0, -1, "COMMENT", 0, 0, "", text});
  }

  // Barrera global: sincroniza todos los PE clocks al maximo
  void barrier() {
    if (pe_clocks_.empty()) return;
    std::uint64_t max_clk = 0;
    for (auto& kv : pe_clocks_) max_clk = std::max(max_clk, kv.second);
    for (int i = 0; i < num_pes; ++i) pe_clocks_[i] = max_clk;
  }

  // Estado para bloques parallel
  struct ParallelState {
    DependencyTracker dep;
    std::unordered_map<int, std::uint64_t> pe_clocks;
  };

  ParallelState begin_parallel() const {
    ParallelState s;
    s.dep = dep_.clone();
    s.pe_clocks = pe_clocks_;
    return s;
  }

  void add_parallel_op(ParallelState& state,
                       int pe_id, const std::string& op,
                       std::uint64_t addr, int size,
                       const std::string& value,
                       const std::string& comment = "") {
    // Tick base del bloque: max de los PEs implicados antes de entrar
    std::uint64_t base = pe_clocks_.count(pe_id) ? pe_clocks_.at(pe_id) : 0;
    const std::uint64_t t_dep = state.dep.next_safe(addr);
    const std::uint64_t t_loc = state.pe_clocks.count(pe_id)
                                ? state.pe_clocks.at(pe_id) : base;
    const std::uint64_t tick  = std::max({base, t_dep, t_loc});

    entries_.push_back(TraceEntry{tick, pe_id, op, addr, size, value, comment});

    const std::uint64_t cost = (to_lower(op) == "add" || to_lower(op) == "sub")
                               ? 2 * MISS_LATENCY : MISS_LATENCY;
    state.pe_clocks[pe_id] = tick + cost;
    if (is_modifying(op)) {
      const std::uint64_t dep_tick = (to_lower(op) == "add" || to_lower(op) == "sub")
                                     ? tick + MISS_LATENCY
                                     : tick;
      state.dep.write(addr, dep_tick);
    }
  }

  void end_parallel(const ParallelState& state) {
    // Propagar el estado mas avanzado al builder
    for (auto& kv : state.pe_clocks) {
      auto it = pe_clocks_.find(kv.first);
      if (it == pe_clocks_.end() || kv.second > it->second)
        pe_clocks_[kv.first] = kv.second;
    }
    dep_ = state.dep;
  }

  // Escribe el trace al stream
  void write_to(std::ostream& os) const {
    os << "# CE4302 trace v2\n";
    if (!name.empty())        os << "# Workload: " << name << "\n";
    if (!description.empty()) os << "# " << description << "\n";
    os << "# MISS_LATENCY=" << MISS_LATENCY << " ns"
       << "  (L1=" << L1_LATENCY
       << " IC="   << IC_LATENCY
       << " MEM="  << MEM_LATENCY << ")\n";
    os << "#\n# tick  pe  op     addr        size  valor\n";

    // Separar comentarios de entradas reales
    std::vector<const TraceEntry*> real;
    for (const auto& e : entries_)
      if (e.op != "COMMENT") real.push_back(&e);

    std::stable_sort(real.begin(), real.end(),
      [](const TraceEntry* a, const TraceEntry* b) {
        return a->tick != b->tick ? a->tick < b->tick : a->pe_id < b->pe_id;
      });

    for (const TraceEntry* e : real) {
      std::ostringstream line;
      line << e->tick << " " << e->pe_id << " " << e->op
           << " 0x" << std::hex << e->addr << std::dec
           << " " << e->size;
      if (!e->value.empty()) line << " " << e->value;
      if (!e->comment.empty()) line << "  # " << e->comment;
      os << line.str() << "\n";
    }
  }

  int total_ops() const {
    int n = 0;
    for (const auto& e : entries_) if (e.op != "COMMENT") ++n;
    return n;
  }

private:
  std::vector<TraceEntry> entries_;
  DependencyTracker dep_;
  std::unordered_map<int, std::uint64_t> pe_clocks_;
};

// ===============================================================
//  LEXER
// ===============================================================
enum class TokenKind {
  Workload, Description, Pes, Parallel, Barrier, Repeat,
  Pe, Colon, LBrace, RBrace, LParen, RParen,
  Op,       // read write add sub
  Number,   // decimal or hex
  String,   // "..."
  IVar,     // $i $j
  Eof
};

struct Token {
  TokenKind   kind;
  std::string value;
  int         line;
};

class LexError : public std::runtime_error {
public:
  explicit LexError(const std::string& msg) : std::runtime_error(msg) {}
};

class Lexer {
public:
  explicit Lexer(const std::string& src)
      : src_(src), pos_(0), line_(1) {}

  std::vector<Token> tokenize() {
    std::vector<Token> tokens;
    while (true) {
      skip_whitespace_and_comments();
      if (pos_ >= src_.size()) {
        tokens.push_back(Token{TokenKind::Eof, "", line_});
        break;
      }
      tokens.push_back(next_token());
    }
    return tokens;
  }

private:
  const std::string& src_;
  std::size_t pos_;
  int line_;

  char peek(int offset = 0) const {
    std::size_t p = pos_ + offset;
    return p < src_.size() ? src_[p] : '\0';
  }

  char advance() {
    char c = src_[pos_++];
    if (c == '\n') ++line_;
    return c;
  }

  void skip_whitespace_and_comments() {
    while (pos_ < src_.size()) {
      if (std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
      } else if (peek() == '#') {
        while (pos_ < src_.size() && peek() != '\n') advance();
      } else {
        break;
      }
    }
  }

  Token next_token() {
    const int ln = line_;
    char c = peek();

    // String literal
    if (c == '"') {
      advance();
      std::string s;
      while (pos_ < src_.size() && peek() != '"') s += advance();
      if (pos_ < src_.size()) advance(); // closing "
      return Token{TokenKind::String, s, ln};
    }

    // IVar $i
    if (c == '$') {
      advance();
      std::string name;
      while (pos_ < src_.size() &&
             (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        name += advance();
      return Token{TokenKind::IVar, name, ln};
    }

    // Number: hex or decimal (with optional leading -)
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '0' && (peek(1) == 'x' || peek(1) == 'X'))) {
      std::string num;
      if (c == '-') { num += advance(); }
      if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        num += advance(); num += advance(); // 0x
        while (pos_ < src_.size() &&
               std::isxdigit(static_cast<unsigned char>(peek())))
          num += advance();
      } else {
        while (pos_ < src_.size() &&
               std::isdigit(static_cast<unsigned char>(peek())))
          num += advance();
      }
      return Token{TokenKind::Number, num, ln};
    }

    // Symbols
    if (c == '{') { advance(); return Token{TokenKind::LBrace,  "{", ln}; }
    if (c == '}') { advance(); return Token{TokenKind::RBrace,  "}", ln}; }
    if (c == '(') { advance(); return Token{TokenKind::LParen,  "(", ln}; }
    if (c == ')') { advance(); return Token{TokenKind::RParen,  ")", ln}; }
    if (c == ':') { advance(); return Token{TokenKind::Colon,   ":", ln}; }

    // Keyword or identifier
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::string kw;
      while (pos_ < src_.size() &&
             (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        kw += advance();

      const std::string lo = to_lower(kw);
      if (lo == "workload")    return Token{TokenKind::Workload,    kw, ln};
      if (lo == "description") return Token{TokenKind::Description, kw, ln};
      if (lo == "pes")         return Token{TokenKind::Pes,         kw, ln};
      if (lo == "parallel")    return Token{TokenKind::Parallel,    kw, ln};
      if (lo == "barrier")     return Token{TokenKind::Barrier,     kw, ln};
      if (lo == "repeat")      return Token{TokenKind::Repeat,      kw, ln};
      if (lo == "pe")          return Token{TokenKind::Pe,          kw, ln};
      if (lo == "read"  || lo == "r")   return Token{TokenKind::Op, "R",   ln};
      if (lo == "write" || lo == "w")   return Token{TokenKind::Op, "W",   ln};
      if (lo == "add")                  return Token{TokenKind::Op, "ADD", ln};
      if (lo == "sub")                  return Token{TokenKind::Op, "SUB", ln};

      throw LexError("Linea " + std::to_string(ln) +
                     ": token desconocido '" + kw + "'");
    }

    throw LexError("Linea " + std::to_string(ln) +
                   ": caracter inesperado '" + c + "'");
  }
};

// ===============================================================
//  PARSER / COMPILER
// ===============================================================
class ParseError : public std::runtime_error {
public:
  explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

class TraceCompiler {
public:
  TraceBuilder compile(const std::string& source) {
    Lexer lexer(source);
    try {
      tokens_ = lexer.tokenize();
    } catch (const LexError& e) {
      throw ParseError(e.what());
    }
    pos_  = 0;
    vars_ = {};
    tb_   = TraceBuilder{};
    parse_program();
    return tb_;
  }

private:
  std::vector<Token> tokens_;
  std::size_t pos_{0};
  std::unordered_map<std::string, std::int64_t> vars_;
  TraceBuilder tb_;

  // ── Token helpers ───────────────────────────────────────────
  const Token& peek(int offset = 0) const {
    std::size_t p = pos_ + offset;
    if (p >= tokens_.size()) return tokens_.back();
    return tokens_[p];
  }

  Token advance() { return tokens_[pos_++]; }

  Token expect(TokenKind kind, const std::string& what) {
    Token t = advance();
    if (t.kind != kind)
      throw ParseError("Linea " + std::to_string(t.line) +
                       ": se esperaba " + what +
                       ", se obtuvo '" + t.value + "'");
    return t;
  }

  bool at(TokenKind k) const { return peek().kind == k; }

  std::uint64_t parse_number() {
    Token t = expect(TokenKind::Number, "numero");
    try {
      return static_cast<std::uint64_t>(std::stoull(t.value, nullptr, 0));
    } catch (...) {
      throw ParseError("Linea " + std::to_string(t.line) +
                       ": numero invalido '" + t.value + "'");
    }
  }

  // Valor: numero o $var
  std::string parse_value() {
    if (at(TokenKind::IVar)) {
      Token t = advance();
      auto it = vars_.find(t.value);
      if (it == vars_.end())
        throw ParseError("Linea " + std::to_string(t.line) +
                         ": variable $" + t.value + " no definida");
      return std::to_string(it->second);
    }
    return std::to_string(static_cast<std::int64_t>(parse_number()));
  }

  bool peek_is_value() const {
    return at(TokenKind::Number) || at(TokenKind::IVar);
  }

  // ── Gramatica ───────────────────────────────────────────────
  void parse_program() {
    while (!at(TokenKind::Eof)) parse_stmt();
  }

  void parse_stmt() {
    switch (peek().kind) {
      case TokenKind::Workload:    parse_workload();    break;
      case TokenKind::Description: parse_description(); break;
      case TokenKind::Pes:         parse_pes();         break;
      case TokenKind::Parallel:    parse_parallel();    break;
      case TokenKind::Barrier:     advance(); tb_.barrier(); break;
      case TokenKind::Repeat:      parse_repeat();      break;
      case TokenKind::Pe:          parse_pe_stmt(tb_);  break;
      default:
        throw ParseError("Linea " + std::to_string(peek().line) +
                         ": sentencia inesperada '" + peek().value + "'");
    }
  }

  void parse_workload() {
    advance();
    Token t = expect(TokenKind::String, "string");
    tb_.name = t.value;
  }

  void parse_description() {
    advance();
    Token t = expect(TokenKind::String, "string");
    tb_.description = t.value;
  }

  void parse_pes() {
    advance();
    tb_.num_pes = static_cast<int>(parse_number());
  }

  void parse_parallel() {
    advance(); // consume "parallel"
    expect(TokenKind::LBrace, "{");

    auto state = tb_.begin_parallel();

    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof)) {
      parse_pe_stmt_parallel(state);
    }
    expect(TokenKind::RBrace, "}");
    tb_.end_parallel(state);
  }

  void parse_repeat() {
    advance(); // consume "repeat"
    const std::int64_t n = static_cast<std::int64_t>(parse_number());
    expect(TokenKind::LBrace, "{");
    const std::size_t block_start = pos_;

    // Primera pasada: determinar posicion del } de cierre
    std::size_t block_end = pos_;
    {
      int depth = 1;
      std::size_t scan = pos_;
      while (depth > 0 && scan < tokens_.size()) {
        if (tokens_[scan].kind == TokenKind::LBrace) ++depth;
        else if (tokens_[scan].kind == TokenKind::RBrace) --depth;
        ++scan;
      }
      block_end = scan - 1; // posicion del } de cierre
    }

    for (std::int64_t i = 0; i < n; ++i) {
      vars_["i"] = i;
      pos_ = block_start;
      while (pos_ < block_end) parse_stmt();
    }
    vars_.erase("i");
    pos_ = block_end;
    expect(TokenKind::RBrace, "}");
  }

  // Parsea pe(N): op addr [value] y emite al builder
  void parse_pe_stmt(TraceBuilder& tb) {
    expect(TokenKind::Pe, "pe");
    expect(TokenKind::LParen, "(");
    const int pe_id = static_cast<int>(parse_number());
    expect(TokenKind::RParen, ")");
    expect(TokenKind::Colon, ":");

    Token op_tok = advance();
    if (op_tok.kind != TokenKind::Op)
      throw ParseError("Linea " + std::to_string(op_tok.line) +
                       ": operacion invalida '" + op_tok.value +
                       "'. Validas: read, write, add, sub");

    const std::uint64_t addr = parse_number();

    std::string value;
    if (is_modifying(op_tok.value) && peek_is_value())
      value = parse_value();

    tb.add_op(pe_id, op_tok.value, addr, 4, value);
  }

  // Igual pero emite al ParallelState
  void parse_pe_stmt_parallel(TraceBuilder::ParallelState& state) {
    expect(TokenKind::Pe, "pe");
    expect(TokenKind::LParen, "(");
    const int pe_id = static_cast<int>(parse_number());
    expect(TokenKind::RParen, ")");
    expect(TokenKind::Colon, ":");

    Token op_tok = advance();
    if (op_tok.kind != TokenKind::Op)
      throw ParseError("Linea " + std::to_string(op_tok.line) +
                       ": operacion invalida '" + op_tok.value + "'");

    const std::uint64_t addr = parse_number();

    std::string value;
    if (is_modifying(op_tok.value) && peek_is_value())
      value = parse_value();

    tb_.add_parallel_op(state, pe_id, op_tok.value, addr, 4, value);
  }
};

// ===============================================================
//  WORKLOADS PREDEFINIDOS
// ===============================================================
static void write_producer_consumer(TraceBuilder& tb) {
  tb.name        = "producer_consumer";
  tb.description = "Dos pares PE0->PE1 y PE2->PE3 en paralelo (8 rondas)";

  const std::uint64_t A = 0x8000, B = 0x9000;

  for (int round = 0; round < 8; ++round) {
    auto s = tb.begin_parallel();
    tb.add_parallel_op(s, 0, "W", A, 4, std::to_string(round));
    tb.add_parallel_op(s, 2, "W", B, 4, std::to_string(round + 100));
    tb.end_parallel(s);

    auto s2 = tb.begin_parallel();
    tb.add_parallel_op(s2, 1, "R", A, 4, "");
    tb.add_parallel_op(s2, 3, "R", B, 4, "");
    tb.end_parallel(s2);
  }
}

static void write_migratory(TraceBuilder& tb) {
  tb.name        = "migratory";
  tb.description = "Linea A (0xA000) y B (0xB000) migrando en paralelo (6 lapsos)";

  const std::uint64_t A = 0xA000, B = 0xB000;

  for (int lap = 0; lap < 6; ++lap) {
    for (int step = 0; step < 4; ++step) {
      int pe_a = step % 4;
      int pe_b = (step + 1) % 4;

      auto sr = tb.begin_parallel();
      tb.add_parallel_op(sr, pe_a, "R", A, 4, "");
      tb.add_parallel_op(sr, pe_b, "R", B, 4, "");
      tb.end_parallel(sr);

      auto sw = tb.begin_parallel();
      tb.add_parallel_op(sw, pe_a, "W", A, 4, std::to_string(lap * 4 + step));
      tb.add_parallel_op(sw, pe_b, "W", B, 4, std::to_string(100 + lap * 4 + step));
      tb.end_parallel(sw);
    }
  }
}

static void write_add_sub(TraceBuilder& tb) {
  tb.name        = "add_sub_example";
  tb.description = "Contador en 0x8000 modificado con ADD/SUB por multiples PEs";

  const std::uint64_t ADDR = 0x8000;
  tb.add_op(0, "W",   ADDR, 4, "100",  "inicializar contador");
  tb.add_op(1, "ADD", ADDR, 4, "10",   "PE1 incrementa");
  tb.add_op(2, "SUB", ADDR, 4, "5",    "PE2 decrementa");
  tb.add_op(3, "R",   ADDR, 4, "",     "PE3 verifica: espera 105");
  tb.add_op(0, "ADD", ADDR, 4, "1",    "PE0 incrementa de nuevo");
  tb.add_op(1, "SUB", ADDR, 4, "3",    "PE1 decrementa");
  tb.add_op(3, "R",   ADDR, 4, "",     "PE3 verifica: espera 103");
}

// ===============================================================
//  MAIN
// ===============================================================
static void print_usage(const char* argv0) {
  std::cerr
    << "Uso:\n"
    << "  " << argv0 << " --workload pc|migratory|add_sub --output <file.trace>\n"
    << "  " << argv0 << " --compile <file.txt> [--output <file.trace>]\n"
    << "\nWorkloads predefinidos:\n"
    << "  pc        Producer-Consumer (dos pares en paralelo)\n"
    << "  migratory Patron migratorio (dos lineas en paralelo)\n"
    << "  add_sub   Ejemplo con ADD y SUB atomicos\n"
    << "\nCompilador:\n"
    << "  --compile prog.txt  Compila pseudocodigo a trace\n";
}
// ---------------------------------------------------------------
//  Workload 3: estrés de evicción (LRU + write-back)
//
//  Una sola PE recorre más líneas distintas que caben en L1.
//  Debe mantenerse kEvictionSweepLines > mp::kL1NumLines (l1_cache.hpp).
// ---------------------------------------------------------------
static constexpr int kEvictionSweepLines = 12;
static constexpr std::uint64_t kEvictionBaseAddr = 0x10000;

void write_eviction_stress(std::ostream& os) {
  os << "# CE4302 trace v1\n"
     << "# Workload: eviction stress\n"
     << "# PE0 hace escrituras 4 B en " << kEvictionSweepLines
     << " líneas (stride 64 B, base 0x" << std::hex << kEvictionBaseAddr << std::dec << ")\n"
     << "# Tras llenar la L1 (" << kEvictionSweepLines
     << " líneas > kL1NumLines), cada nueva línea expulsa una víctima LRU.\n"
     << "# Si la víctima estaba en M, el modelo emite BusWrBack (64 B).\n"
     << "# ticks separados por MISS_LATENCY=" << MISS_LATENCY
     << " ns para alinear con latencia en empty-cache.\n"
     << "#\n"
     << "# tick  pe  op  addr    size  valor\n";

  std::uint64_t t = 0;
  for (int i = 0; i < kEvictionSweepLines; ++i) {
    const std::uint64_t addr =
        kEvictionBaseAddr + static_cast<std::uint64_t>(i) * 64;
    os << t << " 0 W 0x" << std::hex << addr << std::dec << " 4 " << i << "\n";
    t += MISS_LATENCY;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string workload;
  std::string compile_file;
  std::string output;

  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--workload" && i + 1 < argc) {
      workload = argv[++i];
    } else if (a == "--compile" && i + 1 < argc) {
      compile_file = argv[++i];
    } else if (a == "--output" && i + 1 < argc) {
      output = argv[++i];
    } else {
      print_usage(argv[0]);
      return 2;
    }
  }

  TraceBuilder tb;

  // ── Modo compilador ─────────────────────────────────────────
  if (!compile_file.empty()) {
    std::ifstream in(compile_file);
    if (!in) {
      std::cerr << "Error: no se puede abrir '" << compile_file << "'\n";
      return 3;
    }
    const std::string source{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};

    // Output por defecto: mismo nombre con .trace
    if (output.empty()) {
      output = compile_file;
      const auto dot = output.rfind('.');
      if (dot != std::string::npos) output = output.substr(0, dot);
      output += ".trace";
    }

    TraceCompiler compiler;
    try {
      tb = compiler.compile(source);
    } catch (const ParseError& e) {
      std::cerr << "Error de compilacion: " << e.what() << "\n";
      return 4;
    }

  // ── Modo workload predefinido ────────────────────────────────
  } else if (!workload.empty()) {
    if (output.empty()) {
      print_usage(argv[0]);
      return 2;
    }
    if (workload == "pc" || workload == "producer-consumer")
      write_producer_consumer(tb);
    else if (workload == "migratory" || workload == "migrate")
      write_migratory(tb);
    else if (workload == "add_sub" || workload == "addsub")
      write_add_sub(tb);
    else {
      std::cerr << "Workload desconocido: '" << workload << "'\n";
      print_usage(argv[0]);
      return 2;
    }
  } else {
    print_usage(argv[0]);
    return 2;
  }

  std::ofstream out(output);
  if (!out) {
    std::cerr << "Error: no se puede crear '" << output << "'\n";
    return 3;
  }
  tb.write_to(out);

  if (workload == "pc" || workload == "producer-consumer") {
    write_producer_consumer(out);
  } else if (workload == "migratory" || workload == "migrate") {
    write_migratory(out);
  } else if (workload == "eviction" || workload == "evict") {
    write_eviction_stress(out);
  } else {
    std::cerr << "unknown workload: " << workload << "\n";
    print_usage(argv[0]);
    return 2;
  }

  std::cout << "Trace generado en: " << output << "\n";
  return 0;
}