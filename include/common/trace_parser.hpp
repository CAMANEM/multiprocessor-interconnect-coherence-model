#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

// ---------------------------------------------------------------
//  MemoryOperation
//
//  Operaciones soportadas por el trace:
//    Read  (R)  - lectura simple
//    Write (W)  - escritura simple
//    Add   (ADD)- read-modify-write atomico: mem[addr] += valor
//    Sub   (SUB)- read-modify-write atomico: mem[addr] -= valor
//
//  ADD y SUB se tratan como escrituras para coherencia:
//    MSI   : emiten BusRdX (necesitan exclusividad)
//    Firefly: emiten BusUpd si la linea esta en S, BusRdX si en I
//
//  Para agregar nuevas operaciones en el futuro (MUL, CAS, etc.):
//    1. Agregar el enum aqui
//    2. Agregar el token en trace_parser.cpp (op_from_string)
//    3. Agregar el caso en pe_trace_player.cpp (thread_main)
//    4. Agregar el caso en l1_cache.cpp (b_transport_cpu)
// ---------------------------------------------------------------
enum class MemoryOperation : std::uint8_t {
  Read,   // R
  Write,  // W
  Add,    // ADD  mem[addr] += valor
  Sub     // SUB  mem[addr] -= valor
};

// Devuelve true si la operacion modifica memoria (trata como escritura
// para efectos de coherencia y calculo de dependencias en tracegen)
inline bool is_modifying(MemoryOperation op) {
  return op == MemoryOperation::Write ||
         op == MemoryOperation::Add   ||
         op == MemoryOperation::Sub;
}

// Nombre legible para logs y CSV
inline const char* op_name(MemoryOperation op) {
  switch (op) {
    case MemoryOperation::Read:  return "R";
    case MemoryOperation::Write: return "W";
    case MemoryOperation::Add:   return "ADD";
    case MemoryOperation::Sub:   return "SUB";
  }
  return "?";
}

// ---------------------------------------------------------------
//  TraceEntry
static constexpr int kMaxPes = 4;
// ---------------------------------------------------------------
struct TraceEntry {
  std::uint64_t   tick;
  int             pe_id;
  MemoryOperation op;
  std::uint64_t   address;
  std::uint32_t   size;
  std::string     value;   // operando: para W/ADD/SUB es el valor o delta
};

// ---------------------------------------------------------------
//  TraceFile
//
//  Formato de Trace:
//    tick  pe_id  R|W|ADD|SUB  address  [size]  [valor]
//    '#' inicia comentario, lineas vacias se ignoran
// ---------------------------------------------------------------
class TraceFile {
public:
  bool load(const std::string& path, std::string& error_out);

  const std::vector<TraceEntry>& all_entries() const { return entries_; }

  std::vector<TraceEntry> entries_for_pe(int pe_id) const;

private:
  std::vector<TraceEntry> entries_;
};

}  // namespace mp