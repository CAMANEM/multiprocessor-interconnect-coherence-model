#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <systemc>
#include <tlm_utils/simple_target_socket.h>

namespace mp {

class SharedMemory : public sc_core::sc_module {
public:
  static constexpr std::size_t kSizeBytes = 16 * 1024 * 1024;

  tlm_utils::simple_target_socket<SharedMemory> socket;

  explicit SharedMemory(sc_core::sc_module_name name, const sc_core::sc_time& read_latency,
                        const sc_core::sc_time& write_latency);

  void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
  sc_core::sc_time read_latency_;
  sc_core::sc_time write_latency_;
  std::vector<std::uint8_t> mem_;
};

}  // namespace mp
