#include "monitor.hpp"

#include <ostream>

#include "interconnect.hpp"

namespace mp {

/**
 * Increments transaction count, byte sum, and accumulated latency.
 */
void Monitor::record_bus_transaction(int /*pe_id*/, std::uint64_t bytes,
                                     const sc_core::sc_time& latency,
                                     BusTransaction type) {
  ++bus_transactions_;
  bus_bytes_ += bytes;
  total_latency_ += latency;

  switch (type) {
    case BusTransaction::BusRd:
      ++bus_rd_transactions_;
      break;
    case BusTransaction::BusRdX:
      ++bus_rdx_transactions_;
      break;
    case BusTransaction::BusUpd:
      ++bus_upd_transactions_;
      break;
  }
}

/**
 * No-op placeholder for future coherence FSM transition logging.
 */
void Monitor::on_cache_state_change(int /*cache_id*/, std::uint64_t /*line_addr*/,
                                    CacheStateLabel from, CacheStateLabel to) {
  if (from != to) {
    ++cache_state_transitions_;
  }
}

/**
 * Writes one text line with aggregate counters.
 */
void Monitor::dump_summary_line(std::ostream& os) const {
  os << "bus_txns=" << bus_transactions_ << " bus_bytes=" << bus_bytes_
     << " total_latency=" << total_latency_.to_string()
     << " bus_rd=" << bus_rd_transactions_
     << " bus_rdx=" << bus_rdx_transactions_
     << " bus_upd=" << bus_upd_transactions_
     << " state_transitions=" << cache_state_transitions_;
}

}  // namespace mp
