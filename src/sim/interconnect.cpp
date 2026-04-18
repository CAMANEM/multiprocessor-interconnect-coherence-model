#include <tlm>

#include "interconnect.hpp"
#include "monitor.hpp"

namespace mp {

/**
 * Registers four b_transport handlers on target sockets.
 *
 * @param name SystemC module name
 * @param monitor optional metrics sink
 * @param hop_latency fixed per-hop bus latency
 */
Interconnect::Interconnect(sc_core::sc_module_name name, Monitor* monitor,
                           const sc_core::sc_time& hop_latency)
    : sc_module(name),
      tgt0("tgt0"),
      tgt1("tgt1"),
      tgt2("tgt2"),
      tgt3("tgt3"),
      mem_socket("mem_socket"),
      monitor_(monitor),
      hop_latency_(hop_latency) {
  tgt0.register_b_transport(this, &Interconnect::b_transport0);
  tgt1.register_b_transport(this, &Interconnect::b_transport1);
  tgt2.register_b_transport(this, &Interconnect::b_transport2);
  tgt3.register_b_transport(this, &Interconnect::b_transport3);
}

/** Forwards to forward() for input port 0. */
void Interconnect::b_transport0(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  forward(0, trans, delay);
}

/** Forwards to forward() for input port 1. */
void Interconnect::b_transport1(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  forward(1, trans, delay);
}

/** Forwards to forward() for input port 2. */
void Interconnect::b_transport2(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  forward(2, trans, delay);
}

/** Forwards to forward() for input port 3. */
void Interconnect::b_transport3(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  forward(3, trans, delay);
}

/**
 * Adds hop_latency, calls memory, and records time delta and bytes on the monitor.
 */
void Interconnect::forward(int port_id, tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay) {
  const sc_core::sc_time t0 = delay;
  delay += hop_latency_;
  mem_socket->b_transport(trans, delay);
  if (monitor_) {
    monitor_->record_bus_transaction(port_id, trans.get_data_length(), delay - t0);
  }
}

}  // namespace mp
