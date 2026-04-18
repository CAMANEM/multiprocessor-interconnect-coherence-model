#include "top.hpp"

#include <string>

namespace mp {

Top::Top(sc_core::sc_module_name name, const TraceFile& trace, CoherenceProtocolKind protocol)
    : sc_module(name),
      monitor_(),
      mem_("mem", sc_core::sc_time(40, sc_core::SC_NS), sc_core::sc_time(40, sc_core::SC_NS)),
      ic_("ic", &monitor_, sc_core::sc_time(2, sc_core::SC_NS)) {
  for (int i = 0; i < 4; ++i) {
    const std::string l1_name = std::string("l1_") + std::to_string(i);
    l1_[i] = std::make_unique<L1Cache>(l1_name.c_str(), i, &monitor_, protocol,
                                        sc_core::sc_time(1, sc_core::SC_NS));
  }

  for (int i = 0; i < 4; ++i) {
    const std::string pe_name = std::string("pe_") + std::to_string(i);
    pe_[i] = std::make_unique<PeTracePlayer>(pe_name.c_str(), i, trace.entries_for_pe(i));
  }

  pe_[0]->socket.bind(l1_[0]->cpu_socket);
  pe_[1]->socket.bind(l1_[1]->cpu_socket);
  pe_[2]->socket.bind(l1_[2]->cpu_socket);
  pe_[3]->socket.bind(l1_[3]->cpu_socket);

  l1_[0]->mem_socket.bind(ic_.tgt0);
  l1_[1]->mem_socket.bind(ic_.tgt1);
  l1_[2]->mem_socket.bind(ic_.tgt2);
  l1_[3]->mem_socket.bind(ic_.tgt3);

  ic_.mem_socket.bind(mem_.socket);
}

}  // namespace mp
