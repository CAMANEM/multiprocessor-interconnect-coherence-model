#include "pe_trace_player.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

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
 * Fills the buffer with the value from the trace entry
 *
 * @param value_tok  value token from the trace
 * @param buf        output byte buffer
 * @param size       bytes to fill
 */
static void encode_value(const std::string& value_tok,
                         std::vector<unsigned char>& buf,
                         std::uint32_t size) {
  std::fill(buf.begin(), buf.end(), 0);

  if (value_tok.empty()) return;

  // For string
  if (value_tok.size() >= 2 &&
      value_tok.front() == '"' && value_tok.back() == '"') {
    const std::string content = value_tok.substr(1, value_tok.size() - 2);
    const std::uint32_t copy_len = static_cast<std::uint32_t>(std::min<std::size_t>(content.size(), size));
    std::memcpy(buf.data(), content.data(), copy_len);
    return;
  }

  // For char
  if (value_tok.size() >= 3 &&
      value_tok.front() == '\'' && value_tok.back() == '\'') {
    buf[0] = static_cast<unsigned char>(value_tok[1]);
    return;
  }

  // For integer
  try {
    std::size_t idx = 0;
    unsigned long long v = std::stoull(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      for (std::uint32_t i = 0; i < size && i < 8; ++i) {
        buf[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
      }
      return;
    }
  } catch (...) {}

  // For signed integer
  try {
    std::size_t idx = 0;
    long long v = std::stoll(value_tok, &idx, 0);
    if (idx == value_tok.size()) {
      unsigned long long uv = static_cast<unsigned long long>(v);
      for (std::uint32_t i = 0; i < size && i < 8; ++i) {
        buf[i] = static_cast<unsigned char>((uv >> (8 * i)) & 0xFF);
      }
      return;
    }
  } catch (...) {}

  // For others: treat as raw string data
  const std::uint32_t copy_len =
      static_cast<std::uint32_t>(std::min<std::size_t>(value_tok.size(), size));
  std::memcpy(buf.data(), value_tok.data(), copy_len);
}

/**
 * Formats buffer to show a understandable value
 *
 * @param buf  buffer to format
 * @return     value formatted as string
 */
static std::string format_buf(const std::vector<unsigned char>& buf) {
  std::ostringstream oss;
  oss << "[ ";
  for (unsigned char b : buf) {
    oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(b) << " ";
  }
  oss << "]";
  const auto len = buf.size();
  if (len == 1 || len == 2 || len == 4 || len == 8) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < len; ++i) {
      v |= static_cast<std::uint64_t>(buf[i]) << (8 * i);
    }
    oss << std::dec << " (=" << v << " / 0x" << std::hex << v << ")";
  }
  return oss.str();
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
      std::cout << "[PE  " << pe_id_ << "] DISPATCH R"
                << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << e.address
                << std::dec << "  len=" << e.size << "\n";
    } else {
      trans.set_command(tlm::TLM_WRITE_COMMAND);
      // Fills buffer from the actual value stored in the trace entry
      encode_value(e.value, buf, e.size);
      std::cout << "[PE  " << pe_id_ << "] DISPATCH W"
                << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << e.address
                << std::dec << "  len=" << e.size
                << "  value=" << (e.value.empty() ? "(none)" : e.value)
                << "  encoded=" << format_buf(buf) << "\n";
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
