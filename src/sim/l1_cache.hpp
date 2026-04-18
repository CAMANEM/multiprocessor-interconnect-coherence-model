#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

class Monitor;

enum class CoherenceProtocolKind { Msi, Firefly };

class L1Cache : public sc_core::sc_module {
public:
  tlm_utils::simple_target_socket<L1Cache> cpu_socket;
  tlm_utils::simple_initiator_socket<L1Cache> mem_socket;

  explicit L1Cache(sc_core::sc_module_name name, int cache_id, Monitor* monitor,
                    CoherenceProtocolKind protocol, const sc_core::sc_time& lookup_latency);

  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
  int cache_id_{0};
  Monitor* monitor_{nullptr};
  CoherenceProtocolKind protocol_{CoherenceProtocolKind::Msi};
  sc_core::sc_time lookup_latency_{sc_core::SC_ZERO_TIME};
};

}  // namespace mp
