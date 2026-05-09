#include "pe_trace_player.hpp"

#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdint>

#include <tlm>
#include "event_log.hpp"
#include "log.hpp"
#include "monitor.hpp"

namespace mp {

PeTracePlayer::PeTracePlayer(sc_core::sc_module_name name, int pe_id,
                             std::vector<TraceEntry> entries,
                             Monitor* monitor)
    : sc_module(name), socket("socket"), pe_id_(pe_id),
      entries_(std::move(entries)), monitor_(monitor) {
  SC_THREAD(thread_main);
}

// ---------------------------------------------------------------
//  Helpers de conversion
// ---------------------------------------------------------------
static void encode_value(const std::string& tok,
                         std::vector<unsigned char>& buf,
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

static std::int64_t buf_to_int64(const std::vector<unsigned char>& buf) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < buf.size() && i < 8; ++i)
    v |= static_cast<std::uint64_t>(buf[i]) << (8 * i);
  return static_cast<std::int64_t>(v);
}

static void int64_to_buf(std::int64_t val, std::vector<unsigned char>& buf) {
  std::uint64_t uv = static_cast<std::uint64_t>(val);
  for (std::size_t i = 0; i < buf.size() && i < 8; ++i)
    buf[i] = static_cast<unsigned char>((uv >> (8 * i)) & 0xFF);
}

static std::string fmt_val(const std::vector<unsigned char>& buf) {
  if (buf.empty()) return "(sin datos)";
  const auto len = buf.size();
  if (len == 1 || len == 2 || len == 4 || len == 8) {
    std::int64_t v = buf_to_int64(buf);
    std::ostringstream oss;
    oss << v << " (0x" << std::hex << static_cast<std::uint64_t>(v) << ")";
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

// Helper para construir y enviar un TLM transaction
static tlm::tlm_response_status do_tlm(
    tlm_utils::simple_initiator_socket<PeTracePlayer>& socket,
    std::uint64_t address, std::uint32_t size,
    std::vector<unsigned char>& buf,
    tlm::tlm_command cmd,
    sc_core::sc_time& delay_accum) {

  tlm::tlm_generic_payload trans;
  trans.set_address(address);
  trans.set_data_length(size);
  trans.set_streaming_width(size);
  trans.set_data_ptr(buf.data());
  trans.set_command(cmd);
  trans.set_byte_enable_ptr(nullptr);
  trans.set_dmi_allowed(false);
  trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

  sc_core::sc_time d = sc_core::SC_ZERO_TIME;
  socket->b_transport(trans, d);
  // wait se llama fuera para poder acumular delays de RMW
  delay_accum += d;
  return trans.get_response_status();
}

// ---------------------------------------------------------------
//  thread_main
// ---------------------------------------------------------------
void PeTracePlayer::thread_main() {
  std::size_t op_num = 0;

  for (const TraceEntry& e : entries_) {
    ++op_num;

    const sc_core::sc_time abs_time(static_cast<double>(e.tick), sc_core::SC_NS);
    if (abs_time > sc_core::sc_time_stamp())
      wait(abs_time - sc_core::sc_time_stamp());

    const sc_core::sc_time t_launch = sc_core::sc_time_stamp();
    sc_core::sc_time total_delay    = sc_core::SC_ZERO_TIME;
    std::vector<unsigned char> buf(e.size, 0);
    std::string op_label;
    std::string value_info;
    bool ok = true;

    switch (e.op) {

      // ── LECTURA ─────────────────────────────────────────────
      case MemoryOperation::Read: {
        op_label = "LECTURA (R)";
        auto status = do_tlm(socket, e.address, e.size, buf,
                             tlm::TLM_READ_COMMAND, total_delay);
        wait(total_delay);
        ok = (status == tlm::TLM_OK_RESPONSE);
        value_info = ok ? "Valor leido: " + fmt_val(buf)
                        : "[ERROR] status=" + std::to_string(static_cast<int>(status));
        if (monitor_) monitor_->record_pe_operation(true, false, false);
        break;
      }

      // ── ESCRITURA ───────────────────────────────────────────
      case MemoryOperation::Write: {
        op_label = "ESCRITURA (W)";
        encode_value(e.value, buf, e.size);
        value_info = "Valor escrito: " +
                     (e.value.empty() ? "(cero)" : e.value) +
                     "  ->  " + fmt_val(buf);
        auto status = do_tlm(socket, e.address, e.size, buf,
                             tlm::TLM_WRITE_COMMAND, total_delay);
        wait(total_delay);
        ok = (status == tlm::TLM_OK_RESPONSE);
        if (!ok) value_info += " [ERROR] status=" +
                               std::to_string(static_cast<int>(status));
        if (monitor_) monitor_->record_pe_operation(false, false, false);
        break;
      }

      // ── ADD: read-modify-write atomico mem[addr] += delta ───
      case MemoryOperation::Add: {
        op_label = "ADD (RMW)";

        // Paso 1: leer valor actual
        std::vector<unsigned char> read_buf(e.size, 0);
        sc_core::sc_time d1 = sc_core::SC_ZERO_TIME;
        auto s1 = do_tlm(socket, e.address, e.size, read_buf,
                         tlm::TLM_READ_COMMAND, d1);
        wait(d1);
        total_delay += d1;

        if (s1 != tlm::TLM_OK_RESPONSE) {
          ok = false;
          value_info = "[ERROR en lectura RMW] status=" +
                       std::to_string(static_cast<int>(s1));
          break;
        }

        // Paso 2: calcular resultado
        std::vector<unsigned char> delta_buf(e.size, 0);
        encode_value(e.value, delta_buf, e.size);
        const std::int64_t current = buf_to_int64(read_buf);
        const std::int64_t delta   = buf_to_int64(delta_buf);
        const std::int64_t result  = current + delta;
        int64_to_buf(result, buf);

        value_info = "ADD: " + std::to_string(current) +
                     " + "   + std::to_string(delta)   +
                     " = "   + std::to_string(result)  +
                     "  ->  " + fmt_val(buf);

        // Paso 3: escribir resultado
        sc_core::sc_time d2 = sc_core::SC_ZERO_TIME;
        auto s2 = do_tlm(socket, e.address, e.size, buf,
                         tlm::TLM_WRITE_COMMAND, d2);
        wait(d2);
        total_delay += d2;

        ok = (s2 == tlm::TLM_OK_RESPONSE);
        if (!ok) value_info += " [ERROR en escritura RMW] status=" +
                               std::to_string(static_cast<int>(s2));
        if (monitor_) monitor_->record_pe_operation(false, true, false);
        break;
      }

      // ── SUB: read-modify-write atomico mem[addr] -= delta ───
      case MemoryOperation::Sub: {
        op_label = "SUB (RMW)";

        std::vector<unsigned char> read_buf(e.size, 0);
        sc_core::sc_time d1 = sc_core::SC_ZERO_TIME;
        auto s1 = do_tlm(socket, e.address, e.size, read_buf,
                         tlm::TLM_READ_COMMAND, d1);
        wait(d1);
        total_delay += d1;

        if (s1 != tlm::TLM_OK_RESPONSE) {
          ok = false;
          value_info = "[ERROR en lectura RMW] status=" +
                       std::to_string(static_cast<int>(s1));
          break;
        }

        std::vector<unsigned char> delta_buf(e.size, 0);
        encode_value(e.value, delta_buf, e.size);
        const std::int64_t current = buf_to_int64(read_buf);
        const std::int64_t delta   = buf_to_int64(delta_buf);
        const std::int64_t result  = current - delta;
        int64_to_buf(result, buf);

        value_info = "SUB: " + std::to_string(current) +
                     " - "   + std::to_string(delta)   +
                     " = "   + std::to_string(result)  +
                     "  ->  " + fmt_val(buf);

        sc_core::sc_time d2 = sc_core::SC_ZERO_TIME;
        auto s2 = do_tlm(socket, e.address, e.size, buf,
                         tlm::TLM_WRITE_COMMAND, d2);
        wait(d2);
        total_delay += d2;

        ok = (s2 == tlm::TLM_OK_RESPONSE);
        if (!ok) value_info += " [ERROR en escritura RMW] status=" +
                               std::to_string(static_cast<int>(s2));
        if (monitor_) monitor_->record_pe_operation(false, false, true);
        break;
      }
    }

    // Registrar en EventLog con timestamp de lanzamiento
    std::ostringstream msg;
    msg << "----------------------------------------------------\n"
        << "[PE-" << pe_id_
        << " | tick=" << e.tick << " ns"
        << " | op " << op_num << "/" << entries_.size() << "]\n"
        << "  Operacion : " << op_label << "\n"
        << "  Direccion : 0x" << std::hex << std::setw(8) << std::setfill('0')
        << e.address << std::dec << "\n"
        << "  Tamano    : " << e.size << " bytes\n"
        << "  " << value_info << "\n"
        << "  Latencia acumulada: " << total_delay.to_string() << "\n";

    EventLog::record(t_launch, msg.str());
  }

  std::ostringstream done;
  done << "[PE-" << pe_id_ << "] Todas las operaciones completadas.";
  EventLog::record(sc_core::sc_time_stamp(), done.str());
}

}  // namespace mp