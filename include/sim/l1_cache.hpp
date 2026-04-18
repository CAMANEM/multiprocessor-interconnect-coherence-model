#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

class Monitor;

/** Coherence protocol family selection (logic still pending in the stub). */
enum class CoherenceProtocolKind { Msi, Firefly };

/**
 * Private L1 cache (stub): accepts PE transactions and forwards to the bus without MSI/Firefly FSM.
 */
class L1Cache : public sc_core::sc_module {
public:
  /** CPU/PE side (target): PE is initiator into this socket. */
  tlm_utils::simple_target_socket<L1Cache> cpu_socket;
  /** Memory/bus side (initiator): toward interconnect. */
  tlm_utils::simple_initiator_socket<L1Cache> mem_socket;

  /**
   * @param name SystemC module name
   * @param cache_id numeric id of this L1 (0..3)
   * @param monitor optional statistics/state hook (may be nullptr)
   * @param protocol selected protocol (reserved for future implementation)
   * @param lookup_latency fixed delay added per cache lookup before forward
   */
  explicit L1Cache(sc_core::sc_module_name name, int cache_id, Monitor* monitor,
                   CoherenceProtocolKind protocol, const sc_core::sc_time& lookup_latency);

  /**
   * TLM handler registered on cpu_socket: passthrough to mem_socket with latency and monitor hook.
   *
   * @param trans TLM payload from the PE
   * @param delay transaction time accumulator
   */
  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
  int cache_id_{0};
  Monitor* monitor_{nullptr};
  CoherenceProtocolKind protocol_{CoherenceProtocolKind::Msi};
  sc_core::sc_time lookup_latency_{sc_core::SC_ZERO_TIME};
};

}  // namespace mp
