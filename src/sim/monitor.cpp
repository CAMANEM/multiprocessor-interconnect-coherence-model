#include "monitor.hpp"
#include <ostream>
#include "interconnect.hpp"

namespace mp {

void Monitor::record_bus_transaction(int /*pe_id*/, std::uint64_t bytes,
                                     const sc_core::sc_time& latency,
                                     BusTransaction type) {
  ++bus_transactions_;
  bus_bytes_ += bytes;
  total_latency_ += latency;
  switch (type) {
    case BusTransaction::BusRd:     ++bus_rd_transactions_;  break;
    case BusTransaction::BusRdX:    ++bus_rdx_transactions_; break;
    case BusTransaction::BusUpd:    ++bus_upd_transactions_; break;
    case BusTransaction::BusWrBack: ++bus_wb_transactions_;   break;
  }
}

void Monitor::on_cache_state_change(int /*cache_id*/, std::uint64_t /*line_addr*/,
                                    CacheStateLabel from, CacheStateLabel to) {
  if (from != to) ++cache_state_transitions_;
}

void Monitor::record_pe_operation(bool is_read, bool is_add, bool is_sub) {
  if (is_read)       ++pe_reads_;
  else if (is_add)   ++pe_adds_;
  else if (is_sub)   ++pe_subs_;
  else               ++pe_writes_;
}

void Monitor::dump_summary_line(std::ostream& os) const {
  os << "bus_txns="           << bus_transactions_
     << " bus_bytes="         << bus_bytes_
     << " total_latency="     << total_latency_.to_string()
     << " bus_rd="            << bus_rd_transactions_
     << " bus_rdx="           << bus_rdx_transactions_
     << " bus_upd="           << bus_upd_transactions_
     << " bus_wb="            << bus_wb_transactions_
     << " state_transitions=" << cache_state_transitions_
     << " pe_R="              << pe_reads_
     << " pe_W="              << pe_writes_
     << " pe_ADD="            << pe_adds_
     << " pe_SUB="            << pe_subs_;
}
void Monitor::dump_csv_header(std::ostream& os) {
  os << "trace"
     << ",protocol"
     << ",sim_end_ns"
     // bus
     << ",bus_txns"
     << ",bus_bytes"
     << ",bus_rd"
     << ",bus_rdx"
     << ",bus_upd"
     << ",bus_wb"
     << ",state_trans"
     << ",total_lat_ns"
     << ",avg_lat_ns"
     << ",bw_bytes_per_ns"
     // pe operations
     << ",pe_reads"
     << ",pe_writes"
     << ",pe_adds"
     << ",pe_subs"
     << "\n";
}

void Monitor::dump_csv_row(std::ostream& os,
                           const std::string& trace_name,
                           const std::string& protocol,
                           double sim_end_ns) const {
  const double total_lat_ns = total_latency_.to_double();
  const double avg_lat_ns   = bus_transactions_ > 0
                              ? total_lat_ns / static_cast<double>(bus_transactions_)
                              : 0.0;
  const double bw = sim_end_ns > 0.0
                    ? static_cast<double>(bus_bytes_) / sim_end_ns
                    : 0.0;

  os << trace_name
     << "," << protocol
     << "," << sim_end_ns
     << "," << bus_transactions_
     << "," << bus_bytes_
     << "," << bus_rd_transactions_
     << "," << bus_rdx_transactions_
     << "," << bus_upd_transactions_
     << "," << bus_wb_transactions_
     << "," << cache_state_transitions_
     << "," << total_lat_ns
     << "," << avg_lat_ns
     << "," << bw
     << "," << pe_reads_
     << "," << pe_writes_
     << "," << pe_adds_
     << "," << pe_subs_
     << "\n";
}

}  // namespace mp
