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
    case BusTransaction::BusRd:  ++bus_rd_transactions_;  break;
    case BusTransaction::BusRdX: ++bus_rdx_transactions_; break;
    case BusTransaction::BusUpd: ++bus_upd_transactions_; break;
  }
}

void Monitor::on_cache_state_change(int /*cache_id*/, std::uint64_t /*line_addr*/,
                                    CacheStateLabel from, CacheStateLabel to) {
  if (from != to) ++cache_state_transitions_;
}

void Monitor::dump_summary_line(std::ostream& os) const {
  os << "bus_txns="          << bus_transactions_
     << " bus_bytes="        << bus_bytes_
     << " total_latency="    << total_latency_.to_string()
     << " bus_rd="           << bus_rd_transactions_
     << " bus_rdx="          << bus_rdx_transactions_
     << " bus_upd="          << bus_upd_transactions_
     << " state_transitions="<< cache_state_transitions_;
}

// ---------------------------------------------------------------
//  CSV: cabecera
//
//  Columnas:
//    trace        - nombre del workload
//    protocol     - msi o firefly
//    sim_end_ns   - tiempo total de simulacion en nanosegundos
//    bus_txns     - total de transacciones de bus
//    bus_bytes    - total de bytes transferidos por el bus
//    bus_rd       - cantidad de BusRd (lecturas con miss)
//    bus_rdx      - cantidad de BusRdX (escrituras / upgrades)
//    bus_upd      - cantidad de BusUpd (solo Firefly)
//    state_trans  - transiciones de estado en caches (I->S, S->M, etc.)
//    total_lat_ns - suma de latencias de todas las transacciones (ns)
//    avg_lat_ns   - latencia promedio por transaccion (ns)
//    bw_bytes_per_ns - ancho de banda efectivo (bytes / ns de simulacion)
// ---------------------------------------------------------------
void Monitor::dump_csv_header(std::ostream& os) {
  os << "trace"
     << ",protocol"
     << ",sim_end_ns"
     << ",bus_txns"
     << ",bus_bytes"
     << ",bus_rd"
     << ",bus_rdx"
     << ",bus_upd"
     << ",state_trans"
     << ",total_lat_ns"
     << ",avg_lat_ns"
     << ",bw_bytes_per_ns"
     << "\n";
}

void Monitor::dump_csv_row(std::ostream& os,
                           const std::string& trace_name,
                           const std::string& protocol,
                           double sim_end_ns) const {
  const double total_lat_ns = total_latency_.to_double();  // SystemC SC_NS default
  const double avg_lat_ns   = bus_transactions_ > 0
                              ? total_lat_ns / static_cast<double>(bus_transactions_)
                              : 0.0;
  const double bw           = sim_end_ns > 0.0
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
     << "," << cache_state_transitions_
     << "," << total_lat_ns
     << "," << avg_lat_ns
     << "," << bw
     << "\n";
}

}  // namespace mp