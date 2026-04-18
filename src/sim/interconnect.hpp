#pragma once

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

class Monitor;

class Interconnect : public sc_core::sc_module {
public:
  static constexpr int kPorts = 4;

  tlm_utils::simple_target_socket<Interconnect> tgt0;
  tlm_utils::simple_target_socket<Interconnect> tgt1;
  tlm_utils::simple_target_socket<Interconnect> tgt2;
  tlm_utils::simple_target_socket<Interconnect> tgt3;

  tlm_utils::simple_initiator_socket<Interconnect> mem_socket;

  explicit Interconnect(sc_core::sc_module_name name, Monitor* monitor,
                        const sc_core::sc_time& hop_latency);

private:
  void b_transport0(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
  void b_transport1(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
  void b_transport2(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
  void b_transport3(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  void forward(int port_id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

  Monitor* monitor_{nullptr};
  sc_core::sc_time hop_latency_;
};

}  // namespace mp
