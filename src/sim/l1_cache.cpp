#include <cstdint>

#include <iomanip>
#include <sstream>

#include <tlm>

#include "l1_cache.hpp"
#include "log.hpp"
#include "monitor.hpp"

namespace mp {

namespace {

constexpr std::uint64_t kLineSizeBytes = 64;

/**
 * Aligns an address down to a multiple of align.
 *
 * @param addr byte address
 * @param align line size or alignment divisor
 * @return aligned block start address
 */
std::uint64_t align_down(std::uint64_t addr, std::uint64_t align) {
  return addr - (addr % align);
}

}  // namespace

/**
 * Registers b_transport callback on cpu_socket.
 *
 * @param name SystemC module name
 * @param cache_id cache index
 * @param monitor optional metrics monitor
 * @param protocol protocol selection (reserved)
 * @param lookup_latency fixed lookup delay per access
 */
L1Cache::L1Cache(sc_core::sc_module_name name, int cache_id, Monitor* monitor,
                 CoherenceProtocolKind protocol, const sc_core::sc_time& lookup_latency)
    : sc_module(name),
      cpu_socket("cpu_socket"),
      mem_socket("mem_socket"),
      cache_id_(cache_id),
      monitor_(monitor),
      protocol_(protocol),
      lookup_latency_(lookup_latency) {
  (void)protocol_;
  cpu_socket.register_b_transport(this, &L1Cache::b_transport_cpu);
}

/**
 * Stub: notifies monitor for line, adds lookup_latency, forwards to mem_socket initiator.
 */
void L1Cache::b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  // Stub L1: passthrough to downstream (interconnect/memory). MSI/Firefly state will live here.
  const std::uint64_t line_addr = align_down(trans.get_address(), kLineSizeBytes);
  if (monitor_) {
    monitor_->on_cache_state_change(cache_id_, line_addr, CacheStateLabel::Invalid,
                                    CacheStateLabel::Invalid);
  }

  std::ostringstream os;
  os << "[L1_" << cache_id_ << "] passthrough line=0x" << std::hex << line_addr << std::dec << " cmd="
     << (trans.get_command() == tlm::TLM_READ_COMMAND ? "R" : "W");
  Log::debug(os.str());

  delay += lookup_latency_;
  mem_socket->b_transport(trans, delay);
}

}  // namespace mp
