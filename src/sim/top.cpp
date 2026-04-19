#include "top.hpp"

#include <string>

namespace mp {

/**
 * Instantiates all system components and binds TLM sockets.
 *   PE → L1 Cache → Interconnect → Shared Memory
 *
 * @param name SystemC module name
 * @param trace parsed memory trace file
 * @param protocol selected coherence protocol (MSI / Firefly)
 */
Top::Top(sc_core::sc_module_name name, const TraceFile& trace, CoherenceProtocolKind protocol)
    : sc_module(name),

      // Metrics monitor (shared across all components)
      monitor_(),

      // Shared memory with fixed read/write latencies
      mem_("mem",
           sc_core::sc_time(40, sc_core::SC_NS),  // read latency
           sc_core::sc_time(40, sc_core::SC_NS)), // write latency

      // Interconnect (bus) with fixed hop latency
      ic_("ic", &monitor_, sc_core::sc_time(2, sc_core::SC_NS)) {

  // Instantiate L1 caches
  for (int i = 0; i < 4; ++i) {
    const std::string l1_name = std::string("l1_") + std::to_string(i);

    l1_[i] = std::make_unique<L1Cache>(
        l1_name.c_str(),
        i,                      // cache ID
        &monitor_,              // metrics hook
        protocol,               // coherence protocol
        sc_core::sc_time(1, sc_core::SC_NS) // lookup latency
    );
  }

  // Register caches in interconnect (SNOOPING)
  for (int i = 0; i < 4; ++i) {
    ic_.register_cache(i, l1_[i].get());
  }

  // Instantiate Processing Elements (PEs)
  for (int i = 0; i < 4; ++i) {
    const std::string pe_name = std::string("pe_") + std::to_string(i);

    pe_[i] = std::make_unique<PeTracePlayer>(
        pe_name.c_str(),
        i,                         // PE ID
        trace.entries_for_pe(i)    // subset of trace for this PE
    );
  }

  // Bind PE → L1 connections
  pe_[0]->socket.bind(l1_[0]->cpu_socket);
  pe_[1]->socket.bind(l1_[1]->cpu_socket);
  pe_[2]->socket.bind(l1_[2]->cpu_socket);
  pe_[3]->socket.bind(l1_[3]->cpu_socket);

  // Bind L1 → Interconnect
  l1_[0]->mem_socket.bind(ic_.tgt0);
  l1_[1]->mem_socket.bind(ic_.tgt1);
  l1_[2]->mem_socket.bind(ic_.tgt2);
  l1_[3]->mem_socket.bind(ic_.tgt3);


  // Bind Interconnect → Memory
  ic_.mem_socket.bind(mem_.socket);
}

}  // namespace mp