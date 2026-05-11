#include "shared_memory.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <tlm>
#include "event_log.hpp"
#include "log.hpp"

namespace mp {

SharedMemory::SharedMemory(sc_core::sc_module_name name,
                           const sc_core::sc_time& read_latency,
                           const sc_core::sc_time& write_latency)
    : sc_module(name), socket("socket"),
      read_latency_(read_latency), write_latency_(write_latency),
      mem_(kSizeBytes, 0) {
  socket.register_b_transport(this, &SharedMemory::b_transport);
}

namespace {
std::string fmt(const unsigned char* ptr, unsigned int len) {
  if (!ptr || len == 0) return "(sin datos)";
  if (len == 1 || len == 2 || len == 4 || len == 8) {
    std::uint64_t v = 0;
    for (unsigned int i = 0; i < len; ++i)
      v |= static_cast<std::uint64_t>(ptr[i]) << (8 * i);
    std::ostringstream oss;
    oss << v << " (0x" << std::hex << v << ")";
    return oss.str();
  }
  // Writeback de linea completa (64 bytes): mostrar solo los bytes
  // significativos (no cero) como numero. La cache almacena el dato
  // en los primeros bytes; el resto de la linea son cero.
  unsigned int sig = len;
  while (sig > 1 && ptr[sig - 1] == 0x00) --sig;
  if (sig <= 8) {
    std::uint64_t v = 0;
    for (unsigned int i = 0; i < sig; ++i)
      v |= static_cast<std::uint64_t>(ptr[i]) << (8 * i);
    std::ostringstream oss;
    oss << v << " (0x" << std::hex << v << ")"
        << std::dec << " [wb " << len << "B]";
    return oss.str();
  }
  std::ostringstream oss;
  oss << "[ ";
  for (unsigned int i = 0; i < len && i < 8; ++i)
    oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(ptr[i]) << " ";
  oss << "... ]";
  return oss.str();
}
}  // namespace

void SharedMemory::b_transport(tlm::tlm_generic_payload& trans,
                                sc_core::sc_time& delay) {
  tlm::tlm_command cmd  = trans.get_command();
  sc_dt::uint64    addr = trans.get_address();
  unsigned char*   ptr  = trans.get_data_ptr();
  unsigned int     len  = trans.get_data_length();

  if (addr + len > mem_.size()) {
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return;
  }

  const sc_core::sc_time t = sc_core::sc_time_stamp();
  std::ostringstream msg;

  if (cmd == tlm::TLM_READ_COMMAND) {
    std::memcpy(ptr, mem_.data() + addr, len);
    delay += read_latency_;
    msg << "  [MEM t=" << t.to_string() << "]"
        << " LECTURA   addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
        << std::dec << "  valor=" << fmt(ptr, len)
        << "  latencia=" << read_latency_.to_string();
  } else if (cmd == tlm::TLM_WRITE_COMMAND) {
    msg << "  [MEM t=" << t.to_string() << "]"
        << " ESCRITURA addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
        << std::dec << "  valor=" << fmt(ptr, len)
        << "  latencia=" << write_latency_.to_string();
    std::memcpy(mem_.data() + addr, ptr, len);
    delay += write_latency_;
  } else {
    trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    return;
  }

  EventLog::record(t, msg.str());
  trans.set_response_status(tlm::TLM_OK_RESPONSE);
  trans.set_dmi_allowed(false);
}

}  // namespace mp