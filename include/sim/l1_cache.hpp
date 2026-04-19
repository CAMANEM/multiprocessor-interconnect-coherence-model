#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <unordered_map>

#include "interconnect.hpp"   // ← BusTransaction lives here now (single source of truth)

namespace mp {

class Monitor;

/** Coherence protocol family selection. */
enum class CoherenceProtocolKind { Msi, Firefly };

/** Cache line coherence states (MSI protocol). */
enum class CacheState { I, S, M };

/** Represents a single cache line entry. */
struct CacheLine {
  CacheState state{ CacheState::I };
};

/**
 * Private L1 cache implementing a simplified MSI coherence protocol.
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

  /**
   * @param name           SystemC module name
   * @param id             cache identifier (0..3)
   * @param monitor        optional metrics collector
   * @param protocol       selected coherence protocol (MSI active)
   * @param latency        fixed lookup latency per access
   */
  L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
          CoherenceProtocolKind protocol, const sc_core::sc_time& latency);

  /**
   * Handles incoming CPU memory requests.
   *
   * @param trans  TLM payload (read/write)
   * @param delay  accumulated transaction delay
   */
  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /**
   * Handles snooped bus transactions from the interconnect.
   *
   * @param addr  accessed memory address
   * @param type  type of coherence transaction (BusRd / BusRdX)
   */
  void snoop(uint64_t addr, BusTransaction type);

private:
  /**
   * Aligns address to cache line boundary.
   *
   * @param addr  raw address
   * @return      aligned address (start of cache line)
   */
  uint64_t align_down(uint64_t addr);

  int                      id_;        /**< Cache identifier */
  Monitor*                 monitor_;   /**< Metrics hook (optional) */
  CoherenceProtocolKind    protocol_;  /**< Selected protocol */
  sc_core::sc_time         latency_;   /**< Lookup latency */

  static constexpr uint64_t kLineSize = 64; /**< Cache line size in bytes */

  /** Fully associative storage: line address → cache line */
  std::unordered_map<uint64_t, CacheLine> lines_;
};

}  // namespace mp