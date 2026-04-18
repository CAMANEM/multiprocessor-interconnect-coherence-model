#pragma once

#include <cstdint>
#include <ostream>

#include <sysc/kernel/sc_time.h>

namespace mp {

/** Cache line state labels (reserved for MSI/Firefly instrumentation). */
enum class CacheStateLabel : std::uint8_t {
  // Placeholders for future MSI/Firefly instrumentation
  Invalid = 0,
  Shared,
  Modified,
  Owned,
  Exclusive
};

/**
 * Lightweight bus metrics (transaction count, bytes, accumulated latency).
 * Not a SystemC module; shared by pointer across components.
 */
class Monitor {
public:
  /**
   * Records one bus transaction (e.g. observed from the interconnect).
   *
   * @param pe_id port or PE index; may be -1 if not applicable
   * @param bytes number of bytes transferred in this transaction
   * @param latency attributed latency (TLM time delta)
   */
  void record_bus_transaction(int pe_id, std::uint64_t bytes, const sc_core::sc_time& latency);

  /**
   * Hook reserved for cache line state transitions (logging / visualization).
   *
   * @param cache_id L1 cache identifier
   * @param line_addr line-aligned address
   * @param from previous state
   * @param to new state
   */
  void on_cache_state_change(int cache_id, std::uint64_t line_addr, CacheStateLabel from,
                             CacheStateLabel to);

  /**
   * @return number of bus transactions recorded
   */
  std::uint64_t bus_transactions() const { return bus_transactions_; }

  /**
   * @return total bytes transferred across recorded transactions
   */
  std::uint64_t bus_bytes() const { return bus_bytes_; }

  /**
   * @return sum of per-transaction recorded latencies
   */
  sc_core::sc_time total_latency() const { return total_latency_; }

  /**
   * Writes one human-readable summary line with aggregate counters.
   *
   * @param os output stream (e.g. std::cout)
   */
  void dump_summary_line(std::ostream& os) const;

private:
  std::uint64_t bus_transactions_{0};
  std::uint64_t bus_bytes_{0};
  sc_core::sc_time total_latency_{sc_core::SC_ZERO_TIME};
};

}  // namespace mp
