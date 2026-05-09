#pragma once

#include <vector>
#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include "trace_parser.hpp"

namespace mp {

class Monitor;

class PeTracePlayer : public sc_core::sc_module {
public:
  tlm_utils::simple_initiator_socket<PeTracePlayer> socket;

  SC_HAS_PROCESS(PeTracePlayer);

  // monitor puede ser nullptr si no se quieren estadisticas de PE
  explicit PeTracePlayer(sc_core::sc_module_name name, int pe_id,
                         std::vector<TraceEntry> entries,
                         Monitor* monitor = nullptr);

private:
  void thread_main();

  int                     pe_id_{0};
  std::vector<TraceEntry> entries_;
  Monitor*                monitor_{nullptr};
};

}  // namespace mp