#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <systemc>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

/**
 * TLM model of linear main memory (byte vector) with read/write latencies.
 */
class SharedMemory : public sc_core::sc_module {
public:
  /** Backing store size for the simulated physical address space. */
  static constexpr std::size_t kSizeBytes = 16 * 1024 * 1024;

  /** Target socket for transactions arriving from the interconnect. */
  tlm_utils::simple_target_socket<SharedMemory> socket;

  /**
   * Builds memory and registers the blocking transport handler.
   *
   * @param name SystemC module name
   * @param read_latency modeled read delay (added to delay in b_transport)
   * @param write_latency modeled write delay
   */
  explicit SharedMemory(sc_core::sc_module_name name, const sc_core::sc_time& read_latency,
                        const sc_core::sc_time& write_latency);

  /**
   * TLM-2.0 blocking transport: copies to/from mem_ according to command.
   *
   * @param trans generic payload (address, command, data pointer, length)
   * @param delay accumulated transaction time; increased by operation latency
   */
  void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
  sc_core::sc_time read_latency_;
  sc_core::sc_time write_latency_;
  std::vector<std::uint8_t> mem_;
};

}  // namespace mp
