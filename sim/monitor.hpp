#pragma once

#include <cstdint>
#include <ostream>

#include <sysc/kernel/sc_time.h>

namespace mp {

enum class CacheStateLabel : std::uint8_t {
  // Placeholders for future MSI/Firefly instrumentation
  Invalid = 0,
  Shared,
  Modified,
  Owned,
  Exclusive
};

class Monitor {
public:
  void record_bus_transaction(int pe_id, std::uint64_t bytes, const sc_core::sc_time& latency);
  void on_cache_state_change(int cache_id, std::uint64_t line_addr, CacheStateLabel from,
                             CacheStateLabel to);

  std::uint64_t bus_transactions() const { return bus_transactions_; }
  std::uint64_t bus_bytes() const { return bus_bytes_; }
  sc_core::sc_time total_latency() const { return total_latency_; }

  void dump_summary_line(std::ostream& os) const;

private:
  std::uint64_t bus_transactions_{0};
  std::uint64_t bus_bytes_{0};
  sc_core::sc_time total_latency_{sc_core::SC_ZERO_TIME};
};

}  // namespace mp
