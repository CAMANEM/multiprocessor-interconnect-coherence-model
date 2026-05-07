#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <sysc/kernel/sc_time.h>

namespace mp {

enum class BusTransaction : std::uint8_t;

enum class CacheStateLabel : std::uint8_t {
  Invalid = 0,
  Shared,
  Modified,
  Owned,
  Exclusive
};

class Monitor {
public:
  void record_bus_transaction(int pe_id, std::uint64_t bytes,
                              const sc_core::sc_time& latency,
                              BusTransaction type);

  void on_cache_state_change(int cache_id, std::uint64_t line_addr,
                             CacheStateLabel from, CacheStateLabel to);

  std::uint64_t bus_transactions()      const { return bus_transactions_; }
  std::uint64_t bus_bytes()             const { return bus_bytes_; }
  std::uint64_t bus_rd_transactions()   const { return bus_rd_transactions_; }
  std::uint64_t bus_rdx_transactions()  const { return bus_rdx_transactions_; }
  std::uint64_t bus_upd_transactions()  const { return bus_upd_transactions_; }
  std::uint64_t bus_wb_transactions()   const { return bus_wb_transactions_; }
  std::uint64_t cache_state_transitions() const { return cache_state_transitions_; }
  sc_core::sc_time total_latency()      const { return total_latency_; }

  // Escribe una linea de texto con todos los contadores (para consola)
  void dump_summary_line(std::ostream& os) const;

  // Escribe cabecera CSV (llamar una vez antes de la primera fila)
  static void dump_csv_header(std::ostream& os);

  // Escribe una fila CSV con los contadores actuales mas metadatos del run
  //   trace_name : nombre del archivo de trace (ej. "producer_consumer")
  //   protocol   : "msi" o "firefly"
  //   sim_end_ns : tiempo final de simulacion en ns
  void dump_csv_row(std::ostream& os,
                    const std::string& trace_name,
                    const std::string& protocol,
                    double sim_end_ns) const;

private:
  std::uint64_t    bus_transactions_{0};
  std::uint64_t    bus_bytes_{0};
  std::uint64_t    bus_rd_transactions_{0};
  std::uint64_t    bus_rdx_transactions_{0};
  std::uint64_t    bus_upd_transactions_{0};
  std::uint64_t    bus_wb_transactions_{0};
  std::uint64_t    cache_state_transitions_{0};
  sc_core::sc_time total_latency_{sc_core::SC_ZERO_TIME};
};

}  // namespace mp