#include "pe_trace_player.hpp"

#include <iostream>

#include <tlm>

namespace mp {

/**
 * Registers the SC_THREAD that will run thread_main.
 *
 * @param name SystemC module name
 * @param pe_id PE identifier
 * @param entries memory accesses assigned to this PE
 */
PeTracePlayer::PeTracePlayer(sc_core::sc_module_name name, int pe_id,
                             std::vector<TraceEntry> entries)
    : sc_module(name), socket("socket"), pe_id_(pe_id), entries_(std::move(entries)) {
  SC_THREAD(thread_main);
}

/**
 * Runs the PE trace: waits until tick (ns), builds payload, calls b_transport.
 */
void PeTracePlayer::thread_main() {
  for (const TraceEntry& e : entries_) {
    const sc_core::sc_time abs_time(static_cast<double>(e.tick), sc_core::SC_NS);
    if (abs_time > sc_core::sc_time_stamp()) {
      wait(abs_time - sc_core::sc_time_stamp());
    }

    tlm::tlm_generic_payload trans;
    trans.set_address(e.address);
    trans.set_streaming_width(e.size);
    trans.set_data_length(e.size);

    std::vector<unsigned char> buf(e.size, 0);
    trans.set_data_ptr(buf.data());

    if (e.op == MemoryOperation::Read) {
      trans.set_command(tlm::TLM_READ_COMMAND);
    } else {
      trans.set_command(tlm::TLM_WRITE_COMMAND);
      for (std::uint32_t i = 0; i < e.size; ++i) {
        buf[i] = static_cast<unsigned char>((pe_id_ + i) & 0xFF);
      }
    }

    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
    socket->b_transport(trans, local_delay);
    wait(local_delay);

    if (trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
      std::cerr << name() << ": TLM error addr=0x" << std::hex << e.address << std::dec
                << " status=" << static_cast<int>(trans.get_response_status()) << "\n";
    }
  }
}

}  // namespace mp
