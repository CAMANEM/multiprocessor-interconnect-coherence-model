#include "pe_trace_player.hpp"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <tlm>
#include "event_log.hpp"
#include "log.hpp"

namespace mp {

PeTracePlayer::PeTracePlayer(sc_core::sc_module_name name, int pe_id,
                             std::vector<TraceEntry> entries)
    : sc_module(name), socket("socket"), pe_id_(pe_id),
      entries_(std::move(entries)) {
  SC_THREAD(thread_main);
}

static void encode_value(const std::string& tok, std::vector<unsigned char>& buf,
                         std::uint32_t size) {
  std::fill(buf.begin(), buf.end(), 0);
  if (tok.empty()) return;
  if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
    const std::string c = tok.substr(1, tok.size() - 2);
    std::memcpy(buf.data(), c.data(), std::min<std::size_t>(c.size(), size));
    return;
  }
  if (tok.size() >= 3 && tok.front() == '\'' && tok.back() == '\'') {
    buf[0] = static_cast<unsigned char>(tok[1]); return;
  }
  try {
    std::size_t idx = 0;
    unsigned long long v = std::stoull(tok, &idx, 0);
    if (idx == tok.size()) {
      for (std::uint32_t i = 0; i < size && i < 8; ++i)
        buf[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
      return;
    }
  } catch (...) {}
  try {
    std::size_t idx = 0;
    long long v = std::stoll(tok, &idx, 0);
    if (idx == tok.size()) {
      unsigned long long uv = static_cast<unsigned long long>(v);
      for (std::uint32_t i = 0; i < size && i < 8; ++i)
        buf[i] = static_cast<unsigned char>((uv >> (8 * i)) & 0xFF);
      return;
    }
  } catch (...) {}
  std::memcpy(buf.data(), tok.data(), std::min<std::size_t>(tok.size(), size));
}

static std::string fmt_val(const std::vector<unsigned char>& buf) {
  if (buf.empty()) return "(sin datos)";
  const auto len = buf.size();
  if (len == 1 || len == 2 || len == 4 || len == 8) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < len; ++i)
      v |= static_cast<std::uint64_t>(buf[i]) << (8 * i);
    std::ostringstream oss;
    oss << v << " (0x" << std::hex << v << ")";
    return oss.str();
  }
  std::ostringstream oss;
  oss << "[ ";
  for (unsigned char b : buf)
    oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(b) << " ";
  oss << "]";
  return oss.str();
}

void PeTracePlayer::thread_main() {
  std::size_t op_num = 0;
  for (const TraceEntry& e : entries_) {
    ++op_num;
    const sc_core::sc_time abs_time(static_cast<double>(e.tick), sc_core::SC_NS);
    if (abs_time > sc_core::sc_time_stamp())
      wait(abs_time - sc_core::sc_time_stamp());

    const sc_core::sc_time t_launch = sc_core::sc_time_stamp();

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
      encode_value(e.value, buf, e.size);
    }

    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
    socket->b_transport(trans, local_delay);
    wait(local_delay);

    // Registrar el resultado en el log diferido con el tiempo de lanzamiento
    // para que aparezca agrupado con los eventos de L1 y MEM de esta operacion
    std::ostringstream msg;
    msg << "----------------------------------------------------\n"
        << "[PE-" << pe_id_
        << " | tick=" << e.tick << " ns"
        << " | op " << op_num << "/" << entries_.size() << "]\n"
        << "  Operacion : "
        << (e.op == MemoryOperation::Read ? "LECTURA (R)" : "ESCRITURA (W)") << "\n"
        << "  Direccion : 0x" << std::hex << std::setw(8) << std::setfill('0')
        << e.address << std::dec << "\n"
        << "  Tamano    : " << e.size << " bytes\n";

    if (trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
      msg << "  [ERROR] transaccion fallo (status="
          << static_cast<int>(trans.get_response_status()) << ")\n";
    } else {
      if (e.op == MemoryOperation::Write) {
        msg << "  Valor escrito: "
            << (e.value.empty() ? "(cero)" : e.value)
            << "  ->  " << fmt_val(buf) << "\n";
      } else {
        msg << "  Valor leido: " << fmt_val(buf) << "\n";
      }
      msg << "  Latencia acumulada: " << local_delay.to_string() << "\n";
    }

    // Usar t_launch para que este bloque aparezca al inicio de su grupo
    EventLog::record(t_launch, msg.str());
  }

  std::ostringstream done;
  done << "[PE-" << pe_id_ << "] Todas las operaciones completadas.";
  EventLog::record(sc_core::sc_time_stamp(), done.str());
}

}  // namespace mp