#include <iomanip>
#include <sstream>
#include <tlm>
#include "log.hpp"
#include "interconnect.hpp"
#include "l1_cache.hpp"
#include "monitor.hpp"

namespace mp {

/**
 * Constructor: initializes sockets and registers handlers.
 */
Interconnect::Interconnect(sc_core::sc_module_name name, Monitor* monitor,
                           const sc_core::sc_time& latency)
    : sc_module(name),
      tgt0("tgt0"), tgt1("tgt1"), tgt2("tgt2"), tgt3("tgt3"),
      mem_socket("mem_socket"),
      monitor_(monitor),
      latency_(latency) {

  tgt0.register_b_transport(this, &Interconnect::b_transport0);
  tgt1.register_b_transport(this, &Interconnect::b_transport1);
  tgt2.register_b_transport(this, &Interconnect::b_transport2);
  tgt3.register_b_transport(this, &Interconnect::b_transport3);

  caches_.fill(nullptr);
}

/**
 * Registers a cache for snooping.
 */
void Interconnect::register_cache(int id, L1Cache* cache) {
  caches_[id] = cache;
}

/**
 * Broadcasts a snoop to all caches except the requester.
 */
void Interconnect::snoop_all(int requester, uint64_t addr, BusTransaction type) {
  for (int i = 0; i < kPorts; ++i) {
    if (i != requester && caches_[i]) {
      caches_[i]->snoop(addr, type);
    }
  }
}

/**
 * Forwards a transaction to memory and records metrics on the Monitor.
 */
void Interconnect::forward(int id, tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay) {
  const tlm::tlm_command cmd = trans.get_command();
  if (Log::enabled(LogLevel::Info)) {
    std::ostringstream os;
    os << "[IC] port=" << port_id << ' '
     os << "[IC] port=" << id << ' '
       << (cmd == tlm::TLM_READ_COMMAND ? "R" : (cmd == tlm::TLM_WRITE_COMMAND ? "W" : "?"))
       << " addr=0x" << std::hex << trans.get_address() << std::dec << " len="
       << trans.get_data_length() << " hop=" << latency_.to_string();
    Log::info(os.str());
  }

  const uint64_t addr = trans.get_address();

  if (trans.is_write())
    snoop_all(id, addr, BusTransaction::BusRdX);
  else
    snoop_all(id, addr, BusTransaction::BusRd);

  const sc_core::sc_time t0 = delay;

  delay += latency_;
  mem_socket->b_transport(trans, delay);

  if (monitor_) {
    monitor_->record_bus_transaction(
        id,
        static_cast<uint64_t>(trans.get_data_length()),
        delay - t0);
  }
}

/** Port dispatchers */
void Interconnect::b_transport0(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(0, t, d); }
void Interconnect::b_transport1(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(1, t, d); }
void Interconnect::b_transport2(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(2, t, d); }
void Interconnect::b_transport3(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(3, t, d); }

}  // namespace mp