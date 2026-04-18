#include "shared_memory.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>

#include <tlm>

namespace mp {

/**
 * Initializes mem_ and registers b_transport on socket.
 */
SharedMemory::SharedMemory(sc_core::sc_module_name name, const sc_core::sc_time& read_latency,
                           const sc_core::sc_time& write_latency)
    : sc_module(name),
      socket("socket"),
      read_latency_(read_latency),
      write_latency_(write_latency),
      mem_(kSizeBytes, 0) {
  socket.register_b_transport(this, &SharedMemory::b_transport);
}

/**
 * Performs read or write on mem_ with bounds check, delay accounting,
 * and console logging of the data bytes involved.
 */
void SharedMemory::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
  tlm::tlm_command  cmd  = trans.get_command();
  sc_dt::uint64     addr = trans.get_address();
  unsigned char*    ptr  = trans.get_data_ptr();
  unsigned int      len  = trans.get_data_length();

  // Bounds check
  if (addr + len > mem_.size()) {
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return;
  }

  sc_core::sc_time mem_delay = sc_core::SC_ZERO_TIME;

  if (cmd == tlm::TLM_READ_COMMAND) {
    mem_delay = read_latency_;
    std::memcpy(ptr, mem_.data() + addr, len);

    std::cout << "[MEM] READ  addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
              << std::dec << std::setfill(' ')
              << "  len=" << len << "  data=[ ";
    for (unsigned int i = 0; i < len; ++i) {
      std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned int>(ptr[i]) << " ";
    }
    std::cout << std::dec << std::setfill(' ') << "]  lat=" << mem_delay.to_string() << "\n";

  } else if (cmd == tlm::TLM_WRITE_COMMAND) {
    mem_delay = write_latency_;

    std::cout << "[MEM] WRITE addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
              << std::dec << std::setfill(' ')
              << "  len=" << len << "  data=[ ";
    for (unsigned int i = 0; i < len; ++i) {
      std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned int>(ptr[i]) << " ";
    }
    std::cout << std::dec << std::setfill(' ') << "]  lat=" << mem_delay.to_string() << "\n";

    std::memcpy(mem_.data() + addr, ptr, len);

  } else {
    trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    return;
  }

  delay += mem_delay;
  trans.set_response_status(tlm::TLM_OK_RESPONSE);
  trans.set_dmi_allowed(false);
}

}  // namespace mp