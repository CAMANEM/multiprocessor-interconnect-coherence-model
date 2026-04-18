#pragma once

#include <array>
#include <memory>
#include <string>

#include <systemc>

#include "l1_cache.hpp"
#include "interconnect.hpp"
#include "monitor.hpp"
#include "pe_trace_player.hpp"
#include "shared_memory.hpp"
#include "trace_parser.hpp"

namespace mp {

class Top : public sc_core::sc_module {
public:
  Top(sc_core::sc_module_name name, const TraceFile& trace, CoherenceProtocolKind protocol);

  Monitor& monitor() { return monitor_; }

private:
  Monitor monitor_;
  SharedMemory mem_;
  Interconnect ic_;
  std::array<std::unique_ptr<L1Cache>, 4> l1_;
  std::array<std::unique_ptr<PeTracePlayer>, 4> pe_;
};

}  // namespace mp
