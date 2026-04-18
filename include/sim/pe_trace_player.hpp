#pragma once

#include <vector>

#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>

#include "trace_parser.hpp"

namespace mp {

/**
 * Processing element as trace player: issues TLM reads/writes for entries filtered to its pe_id,
 * honoring absolute time per tick (nanoseconds).
 */
class PeTracePlayer : public sc_core::sc_module {
public:
  /** Initiator socket toward the associated L1. */
  tlm_utils::simple_initiator_socket<PeTracePlayer> socket;

  SC_HAS_PROCESS(PeTracePlayer);

  /**
   * @param name SystemC module name
   * @param pe_id PE identifier (must match pe_id in the trace)
   * @param entries access list for this PE (typically from TraceFile::entries_for_pe)
   */
  explicit PeTracePlayer(sc_core::sc_module_name name, int pe_id, std::vector<TraceEntry> entries);

private:
  /**
   * SystemC thread: for each entry waits until tick time then runs b_transport.
   */
  void thread_main();

  int pe_id_{0};
  std::vector<TraceEntry> entries_;
};

}  // namespace mp
