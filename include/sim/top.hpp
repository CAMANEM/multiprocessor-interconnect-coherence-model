#pragma once

#include <array>
#include <memory>
#include <string>

#include <systemc>

#include "interconnect.hpp"
#include "l1_cache.hpp"
#include "monitor.hpp"
#include "pe_trace_player.hpp"
#include "shared_memory.hpp"
#include "trace_parser.hpp"

namespace mp {

/**
 * Top module: four PEs, four L1s, one interconnect and shared memory, wired with TLM.
 */
class Top : public sc_core::sc_module {
public:
  /**
   * Builds hierarchy, creates submodules, and binds PE→L1→interconnect→memory sockets.
   *
   * @param name SystemC module name
   * @param trace loaded trace (split per PE via entries_for_pe)
   * @param protocol MSI/Firefly selection forwarded to L1s (stub)
   */
  Top(sc_core::sc_module_name name, const TraceFile& trace, CoherenceProtocolKind protocol);

  /**
   * @return reference to the system metrics monitor
   */
  Monitor& monitor() { return monitor_; }

private:
  Monitor monitor_;
  SharedMemory mem_;
  Interconnect ic_;
  std::array<std::unique_ptr<L1Cache>, 4> l1_;
  std::array<std::unique_ptr<PeTracePlayer>, 4> pe_;
};

}  // namespace mp
