#include "monitor.hpp"

#include <ostream>

namespace mp {

void Monitor::record_bus_transaction(int /*pe_id*/, std::uint64_t bytes,
                                     const sc_core::sc_time& latency) {
  ++bus_transactions_;
  bus_bytes_ += bytes;
  total_latency_ += latency;
}

void Monitor::on_cache_state_change(int /*cache_id*/, std::uint64_t /*line_addr*/,
                                    CacheStateLabel /*from*/, CacheStateLabel /*to*/) {
  // Reserved for transition logs / visualization (MSI vs Firefly).
}

void Monitor::dump_summary_line(std::ostream& os) const {
  os << "bus_txns=" << bus_transactions_ << " bus_bytes=" << bus_bytes_
     << " total_latency=" << total_latency_.to_string();
}

}  // namespace mp
