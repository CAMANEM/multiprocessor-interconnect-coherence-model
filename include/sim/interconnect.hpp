#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

class Monitor;

/**
 * Simplified bus/interconnect: four target ports (one per L1) and one initiator to memory.
 * Forwards b_transport and optionally reports metrics to Monitor.
 */
class Interconnect : public sc_core::sc_module {
public:
  /** Number of ports toward L1 caches. */
  static constexpr int kPorts = 4;

  tlm_utils::simple_target_socket<Interconnect> tgt0;
  tlm_utils::simple_target_socket<Interconnect> tgt1;
  tlm_utils::simple_target_socket<Interconnect> tgt2;
  tlm_utils::simple_target_socket<Interconnect> tgt3;

  /** Initiator connected to SharedMemory. */
  tlm_utils::simple_initiator_socket<Interconnect> mem_socket;

  /**
   * @param name SystemC module name
   * @param monitor metrics sink pointer (may be nullptr)
   * @param hop_latency fixed bus hop latency before memory
   */
  explicit Interconnect(sc_core::sc_module_name name, Monitor* monitor,
                        const sc_core::sc_time& hop_latency);

private:
  /**
   * TLM entry for transactions from port 0 (PE0 / L1_0).
   *
   * @param trans TLM payload
   * @param delay transaction time accumulator
   */
  void b_transport0(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /**
   * TLM entry for transactions from port 1.
   *
   * @param trans TLM payload
   * @param delay transaction time accumulator
   */
  void b_transport1(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /**
   * TLM entry for transactions from port 2.
   *
   * @param trans TLM payload
   * @param delay transaction time accumulator
   */
  void b_transport2(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /**
   * TLM entry for transactions from port 3.
   *
   * @param trans TLM payload
   * @param delay transaction time accumulator
   */
  void b_transport3(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  /**
   * Forwards the transaction to memory, adds hop_latency, and records statistics.
   *
   * @param port_id input port index 0..3
   * @param trans TLM payload
   * @param delay transaction time accumulator
   */
  void forward(int port_id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  Monitor* monitor_{nullptr};
  sc_core::sc_time hop_latency_;
};

}  // namespace mp
