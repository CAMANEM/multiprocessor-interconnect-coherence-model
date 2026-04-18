#include "l1_cache.hpp"

#include <cstdint>

#include <tlm>

#include "monitor.hpp"

namespace mp {

namespace {

constexpr std::uint64_t kLineSizeBytes = 64;

std::uint64_t align_down(std::uint64_t addr, std::uint64_t align) {
  return addr - (addr % align);
}

}  // namespace

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

void L1Cache::b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  // Stub L1: passthrough to downstream (interconnect/memory). MSI/Firefly state will live here.
  const std::uint64_t line_addr = align_down(trans.get_address(), kLineSizeBytes);
  if (monitor_) {
    monitor_->on_cache_state_change(cache_id_, line_addr, CacheStateLabel::Invalid,
                                    CacheStateLabel::Invalid);
  }

  delay += lookup_latency_;
  mem_socket->b_transport(trans, delay);
}

}  // namespace mp
