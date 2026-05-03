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

  /** 
   * Notifies the interconnect of a change in priority for a specific PE.
   * 
   * @param pe_id PE that has changed priority
   * @param priority new priority: 0 = normal, 1 = high priority
   * */
  void notify_priority(int pe_id, int priority);

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

  std::array<int, kPorts> pending_priority_{};

  /* For round robin scheduling */
  /* Quantum set on 1 for traces with low contention. In case of high contention, quantum on 2 or 3 is recommended */
  static constexpr int kQuantum = 1; 
  sc_core::sc_mutex bus_mutex_;
  int               rr_next_{0};
  /* For tracking the number of consecutive instructions available for round-robin scheduling */
  int               rr_tokens_{kQuantum};
};

}  // namespace mp