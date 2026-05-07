#pragma once

#include <array>
#include <cstddef>
#include <list>
#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <unordered_map>

#include "interconnect.hpp"   // ← BusTransaction lives here now (single source of truth)

namespace mp {

class Monitor;

/** Coherence protocol family selection. */
enum class CoherenceProtocolKind { Msi, Firefly };

/** Cache line coherence states used by MSI and simplified Firefly. */
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
  /** CPU/PE side socket (incoming requests). */
  tlm_utils::simple_target_socket<L1Cache> cpu_socket;

  /** Interconnect side socket (outgoing requests). */
  tlm_utils::simple_initiator_socket<L1Cache> mem_socket;

  /** Builds an L1 cache with selected coherence protocol and fixed lookup latency. */
  L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
          CoherenceProtocolKind protocol, const sc_core::sc_time& latency);

  /**
   * Handles incoming CPU memory requests.
   *
   * @param trans  TLM payload (read/write)
   * @param delay  accumulated transaction delay
   */
  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /** Handles snooped bus transactions from the interconnect. */
  void snoop(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);

private:
  /**
   * Aligns address to cache line boundary.
   *
   * @param addr  raw address
   * @return      aligned address (start of cache line)
   */
  uint64_t align_down(uint64_t addr);

  /** Validates that payload stays inside one cache line. */
  bool payload_fits_line(uint64_t line_addr, const tlm::tlm_generic_payload& trans) const;

  /** Copies payload bytes from cache line into trans data buffer. */
  void read_from_line(uint64_t line_addr, const CacheLine& line,
                      tlm::tlm_generic_payload& trans) const;

  /** Writes trans payload bytes into cache line. */
  void write_to_line(uint64_t line_addr, CacheLine& line,
                     const tlm::tlm_generic_payload& trans);

  /** Applies BusUpd payload bytes into this line when snooping Firefly updates. */
  void apply_bus_update(uint64_t line_addr, CacheLine& line,
                        const tlm::tlm_generic_payload& trans);

  /** Sends a request to interconnect with an explicit coherence transaction hint. */
  void emit_bus_transaction(BusTransaction txn, tlm::tlm_generic_payload& trans,
                            sc_core::sc_time& delay);

  /** Dispatch entry for MSI CPU-side state machine. */
  void handle_cpu_msi(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                      sc_core::sc_time& delay);

  /** Dispatch entry for Firefly CPU-side state machine. */
  void handle_cpu_firefly(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                          sc_core::sc_time& delay);

  /** Handles snooped traffic with MSI transition rules. */
  void snoop_msi(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);

  /** Handles snooped traffic with simplified Firefly transition rules. */
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

  int                      id_;        /**< Cache identifier */
  Monitor*                 monitor_;   /**< Metrics hook (optional) */
  CoherenceProtocolKind    protocol_;  /**< Selected protocol */
  sc_core::sc_time         latency_;   /**< Lookup latency */

  static constexpr uint64_t kLineSize = kCacheLineBytes; /**< Cache line size in bytes */

  /** Valid lines only (states S or M); Invalid entries are removed from the structure. */
  std::unordered_map<uint64_t, CacheLine> lines_;

  /** LRU order: front = victim, back = MRU. */
  std::list<uint64_t> lru_order_;

  std::unordered_map<uint64_t, std::list<uint64_t>::iterator> lru_iter_;
};

}  // namespace mp