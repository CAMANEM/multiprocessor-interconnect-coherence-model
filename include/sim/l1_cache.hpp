#pragma once

#include <array>
#include <cstddef>
#include <list>
#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <unordered_map>

#include "interconnect.hpp"

namespace mp {

class Monitor;

enum class CoherenceProtocolKind { Msi, Firefly };

enum class CacheState { I, S, M };

inline constexpr std::size_t kCacheLineBytes = 64;

/** Number of lines per private L1 (fully associative); capacity = kL1NumLines * kCacheLineBytes. */
inline constexpr std::size_t kL1NumLines = 8;

/** Represents a single cache line entry. */
struct CacheLine {
  CacheState state{ CacheState::I };
  std::array<unsigned char, kCacheLineBytes> data{};
};

/**
 * Private L1 cache implementing selectable MSI/Firefly coherence behavior.
 * Finite fully associative storage with LRU replacement and dirty write-back (BusWrBack).
 *
 * Responsibilities:
 *  - Serve CPU requests (reads/writes)
 *  - Detect hits and misses
 *  - Generate coherence transactions on misses or upgrades
 *  - React to snooped bus transactions from other caches
 */
class L1Cache : public sc_core::sc_module {
public:
  tlm_utils::simple_target_socket<L1Cache>    cpu_socket;
  tlm_utils::simple_initiator_socket<L1Cache> mem_socket;

  L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
          CoherenceProtocolKind protocol, const sc_core::sc_time& latency);

  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
  void snoop(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);

private:
  uint64_t align_down(uint64_t addr);
  bool payload_fits_line(uint64_t line_addr, const tlm::tlm_generic_payload& trans) const;
  void read_from_line(uint64_t line_addr, const CacheLine& line,
                      tlm::tlm_generic_payload& trans) const;
  void write_to_line(uint64_t line_addr, CacheLine& line,
                     const tlm::tlm_generic_payload& trans);
  void apply_bus_update(uint64_t line_addr, CacheLine& line,
                        const tlm::tlm_generic_payload& trans);
  void emit_bus_transaction(BusTransaction txn, tlm::tlm_generic_payload& trans,
                            sc_core::sc_time& delay);

  void handle_cpu_msi(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                      sc_core::sc_time& delay);
  void handle_cpu_firefly(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                          sc_core::sc_time& delay);
  void snoop_msi(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);
  void snoop_firefly(uint64_t addr, BusTransaction type,
                     const tlm::tlm_generic_payload* trans);

  /** Centralizes state updates and monitor transition callbacks. Removes entry when next==I. */
  void set_line_state(uint64_t addr, CacheLine& line, CacheState next_state);

  /** Marks line as most-recently-used (call after CPU hit or miss install). */
  void touch_line(uint64_t line_addr);

  void remove_from_lru(uint64_t line_addr);

  /** If L1 is full and incoming line is absent, evicts LRU victim (write-back if M). */
  void evict_lru_if_full(uint64_t incoming_line_addr, sc_core::sc_time& delay);

  void writeback_dirty_line(uint64_t line_addr, const CacheLine& line,
                            sc_core::sc_time& delay);

  /** String helper for logs. */
  static const char* cache_state_name(CacheState state);

  int                      id_;
  Monitor*                 monitor_;
  CoherenceProtocolKind    protocol_;
  sc_core::sc_time         latency_;

  static constexpr uint64_t kLineSize = kCacheLineBytes;

  /** Valid lines only (states S or M); Invalid entries are removed from the structure. */
  std::unordered_map<uint64_t, CacheLine> lines_;

  /** LRU order: front = victim, back = MRU. */
  std::list<uint64_t> lru_order_;

  std::unordered_map<uint64_t, std::list<uint64_t>::iterator> lru_iter_;
};

}  // namespace mp