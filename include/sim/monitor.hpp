#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <sysc/kernel/sc_time.h>

namespace mp {

enum class BusTransaction : std::uint8_t;

enum class CacheStateLabel : std::uint8_t {
  Invalid = 0, Shared, Modified, Owned, Exclusive
};

class Monitor {
public:
  void record_bus_transaction(int pe_id, std::uint64_t bytes,
                              const sc_core::sc_time& latency,
                              BusTransaction type);

  void on_cache_state_change(int cache_id, std::uint64_t line_addr,
                             CacheStateLabel from, CacheStateLabel to);

  // Contadores de operaciones de PE (llamar desde pe_trace_player)
  void record_pe_operation(bool is_read, bool is_add, bool is_sub);

  std::uint64_t bus_transactions()       const { return bus_transactions_; }
  std::uint64_t bus_bytes()              const { return bus_bytes_; }
  std::uint64_t bus_rd_transactions()    const { return bus_rd_transactions_; }
  std::uint64_t bus_rdx_transactions()   const { return bus_rdx_transactions_; }
  std::uint64_t bus_upd_transactions()   const { return bus_upd_transactions_; }
  std::uint64_t bus_wb_transactions()    const { return bus_wb_transactions_; }
  std::uint64_t cache_state_transitions()const { return cache_state_transitions_; }
  std::uint64_t pe_reads()               const { return pe_reads_; }
  std::uint64_t pe_writes()              const { return pe_writes_; }
  std::uint64_t pe_adds()                const { return pe_adds_; }
  std::uint64_t pe_subs()                const { return pe_subs_; }
  sc_core::sc_time total_latency()       const { return total_latency_; }

  void dump_summary_line(std::ostream& os) const;
  static void dump_csv_header(std::ostream& os);
  void dump_csv_row(std::ostream& os,
                    const std::string& trace_name,
                    const std::string& protocol,
                    double sim_end_ns) const;

private:
  // Bus
  std::uint64_t    bus_transactions_{0};
  std::uint64_t    bus_bytes_{0};
  std::uint64_t    bus_rd_transactions_{0};
  std::uint64_t    bus_rdx_transactions_{0};
  std::uint64_t    bus_upd_transactions_{0};
  std::uint64_t    bus_wb_transactions_{0};
  std::uint64_t    cache_state_transitions_{0};
  sc_core::sc_time total_latency_{sc_core::SC_ZERO_TIME};
  // PE operations
  std::uint64_t    pe_reads_{0};
  std::uint64_t    pe_writes_{0};
  std::uint64_t    pe_adds_{0};
  std::uint64_t    pe_subs_{0};
};

}  // namespace mp
