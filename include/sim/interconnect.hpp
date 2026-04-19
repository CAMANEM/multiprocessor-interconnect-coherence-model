#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>

namespace mp {

class Monitor;
class L1Cache;

/** Coherence transactions supported by the interconnect. */
enum class BusTransaction { BusRd, BusRdX };  

/**
 * Cache-coherent interconnect (bus-based).
 *
 * Responsibilities:
 *  - Forward transactions from L1 caches to shared memory
 *  - Broadcast coherence events to all other caches (snooping)
 */
class Interconnect : public sc_core::sc_module {
public:
  static constexpr int kPorts = 4;

  tlm_utils::simple_target_socket<Interconnect> tgt0;
  tlm_utils::simple_target_socket<Interconnect> tgt1;
  tlm_utils::simple_target_socket<Interconnect> tgt2;
  tlm_utils::simple_target_socket<Interconnect> tgt3;

  tlm_utils::simple_initiator_socket<Interconnect> mem_socket;

  Interconnect(sc_core::sc_module_name name, Monitor* monitor,
               const sc_core::sc_time& latency);

  void register_cache(int id, L1Cache* cache);

private:
  void b_transport0(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport1(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport2(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport3(tlm::tlm_generic_payload&, sc_core::sc_time&);

  void forward(int id, tlm::tlm_generic_payload&, sc_core::sc_time&);
  void snoop_all(int requester, uint64_t addr, BusTransaction type);

  std::array<L1Cache*, kPorts> caches_;

  Monitor*          monitor_{ nullptr };
  sc_core::sc_time  latency_;
};

}  // namespace mp