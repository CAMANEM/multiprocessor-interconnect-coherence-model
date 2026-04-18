#include "l1_cache.hpp"
#include <iomanip>
#include <iostream>

namespace mp {

/**
 * Constructor: initializes sockets and registers TLM callback.
 */
L1Cache::L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
                 CoherenceProtocolKind protocol, const sc_core::sc_time& latency)
    : sc_module(name),
      cpu_socket("cpu_socket"),
      mem_socket("mem_socket"),
      id_(id),
      monitor_(monitor),
      protocol_(protocol),
      latency_(latency) {

  cpu_socket.register_b_transport(this, &L1Cache::b_transport_cpu);
}

/**
 * Align address to cache line boundary.
 */
uint64_t L1Cache::align_down(uint64_t addr) {
  return addr - (addr % kLineSize);
}

/**
 * Main cache access handler.
 */
void L1Cache::b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  uint64_t    addr     = align_down(trans.get_address());
  bool        is_write = trans.is_write();
  const char* op       = is_write ? "W" : "R";

  auto it  = lines_.find(addr);
  bool hit = (it != lines_.end() && it->second.state != CacheState::I);

  // ── HIT ──────────────────────────────────────────────────────────────────
  if (hit) {
    auto& line = it->second;

    if (!is_write) {
      // Read hit (S or M) — served from cache, no bus traffic
      std::cout << "[L1 " << id_ << "] HIT   "
                << op << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                << std::dec << "  state=" << (line.state == CacheState::M ? "M" : "S")
                << " -> served locally\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);  // ← HIT: respuesta OK manual
      return;
    }

    if (line.state == CacheState::M) {
      // Write hit in Modified — already exclusive, no bus traffic
      std::cout << "[L1 " << id_ << "] HIT   "
                << op << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                << std::dec << "  state=M -> served locally\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);  // ← HIT: respuesta OK manual
      return;
    }

    if (line.state == CacheState::S) {
      // Upgrade: S -> M — need exclusive ownership, generates BusRdX
      std::cout << "[L1 " << id_ << "] UPGRD "
                << op << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                << std::dec << "  S->M (BusRdX)\n";
      mem_socket->b_transport(trans, delay);  // ← va al bus, response se setea en memoria
      line.state = CacheState::M;
      delay += latency_;
      return;
    }
  }

  // ── MISS ─────────────────────────────────────────────────────────────────
  CacheState  new_state     = is_write ? CacheState::M : CacheState::S;
  const char* new_state_str = is_write ? "M" : "S";

  std::cout << "[L1 " << id_ << "] MISS  "
            << op << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
            << std::dec << "  I->" << new_state_str
            << (is_write ? " (BusRdX -> mem)" : " (BusRd  -> mem)") << "\n";

  mem_socket->b_transport(trans, delay);  // ← va al bus, response se setea en memoria
  lines_[addr].state = new_state;
  delay += latency_;
}

/**
 * Snooping handler — reacts to other caches' bus transactions.
 */
void L1Cache::snoop(uint64_t addr, BusTransaction type) {
  addr = align_down(addr);

  auto it = lines_.find(addr);
  if (it == lines_.end()) return;

  auto& line = it->second;

  switch (type) {
    case BusTransaction::BusRd:
      // Another cache issued BusRd (read) -> downgrade M -> S
      if (line.state == CacheState::M) {
        std::cout << "[L1 " << id_ << "] SNOOP BusRd  addr=0x"
                  << std::hex << std::setw(8) << std::setfill('0') << addr << std::dec
                  << "  M->S (downgrade)\n";
        line.state = CacheState::S;
      }
      break;

    case BusTransaction::BusRdX:
      // Another cache issued BusRdX (write) -> invalidate
      if (line.state == CacheState::S || line.state == CacheState::M) {
        std::cout << "[L1 " << id_ << "] SNOOP BusRdX addr=0x"
                  << std::hex << std::setw(8) << std::setfill('0') << addr << std::dec
                  << "  " << (line.state == CacheState::M ? "M" : "S") << "->I (invalidate)\n";
        line.state = CacheState::I;
      }
      break;
  }
}

}  // namespace mp