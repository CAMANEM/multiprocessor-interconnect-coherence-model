#include "l1_cache.hpp"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdint>
#include <tlm>
#include "log.hpp"
#include "monitor.hpp"

namespace mp {

namespace {

// Convierte bytes (little-endian) a string con decimal y hex
std::string format_value(const unsigned char* ptr, unsigned int len) {
  if (!ptr || len == 0) return "(sin datos)";
  if (len == 1 || len == 2 || len == 4 || len == 8) {
    std::uint64_t v = 0;
    for (unsigned int i = 0; i < len; ++i)
      v |= static_cast<std::uint64_t>(ptr[i]) << (8 * i);
    std::ostringstream oss;
    oss << v << " (0x" << std::hex << v << ")";
    return oss.str();
  }
  std::ostringstream oss;
  oss << "[ ";
  for (unsigned int i = 0; i < len; ++i)
    oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(ptr[i]) << " ";
  oss << "]";
  return oss.str();
}

std::string format_payload(const tlm::tlm_generic_payload& trans) {
  return format_value(trans.get_data_ptr(), trans.get_data_length());
}

std::string sim_time() {
  return sc_core::sc_time_stamp().to_string();
}

}  // namespace

// ---------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------
L1Cache::L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
                 CoherenceProtocolKind protocol, const sc_core::sc_time& latency)
    : sc_module(name),
      cpu_socket("cpu_socket"),
      mem_socket("mem_socket"),
      id_(id),
      monitor_(monitor),
      protocol_(protocol),
      latency_(latency) {
  cpu_socket.register_b_transport(this, &L1Cache::b_transport_cpu);
}

// ---------------------------------------------------------------
//  Utilidades internas (logica sin cambios)
// ---------------------------------------------------------------
uint64_t L1Cache::align_down(uint64_t addr) {
  return addr - (addr % kLineSize);
}

bool L1Cache::payload_fits_line(uint64_t line_addr,
                                const tlm::tlm_generic_payload& trans) const {
  const uint64_t addr = trans.get_address();
  const uint64_t len  = trans.get_data_length();
  if (len == 0) return true;
  if (addr < line_addr) return false;
  return ((addr - line_addr) + len) <= kLineSize;
}

void L1Cache::read_from_line(uint64_t line_addr, const CacheLine& line,
                             tlm::tlm_generic_payload& trans) const {
  unsigned char*     ptr = trans.get_data_ptr();
  const unsigned int len = trans.get_data_length();
  if (!ptr || len == 0) return;
  if (!payload_fits_line(line_addr, trans)) {
    trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
    return;
  }
  const std::size_t offset =
      static_cast<std::size_t>(trans.get_address() - line_addr);
  std::memcpy(ptr, line.data.data() + offset, len);
}

void L1Cache::write_to_line(uint64_t line_addr, CacheLine& line,
                            const tlm::tlm_generic_payload& trans) {
  const unsigned char* ptr = trans.get_data_ptr();
  const unsigned int   len = trans.get_data_length();
  if (!ptr || len == 0) return;
  if (!payload_fits_line(line_addr, trans)) return;
  const std::size_t offset =
      static_cast<std::size_t>(trans.get_address() - line_addr);
  std::memcpy(line.data.data() + offset, ptr, len);
}

void L1Cache::apply_bus_update(uint64_t line_addr, CacheLine& line,
                               const tlm::tlm_generic_payload& trans) {
  if (!trans.is_write()) return;
  write_to_line(line_addr, line, trans);
}

const char* L1Cache::cache_state_name(CacheState state) {
  switch (state) {
    case CacheState::I: return "I";
    case CacheState::S: return "S";
    case CacheState::M: return "M";
  }
  return "?";
}

void L1Cache::set_line_state(uint64_t addr, CacheLine& line,
                             CacheState next_state) {
  if (line.state == next_state) return;
  const CacheState old_state = line.state;
  line.state = next_state;
  if (!monitor_) return;
  auto to_label = [](CacheState s) {
    switch (s) {
      case CacheState::I: return CacheStateLabel::Invalid;
      case CacheState::S: return CacheStateLabel::Shared;
      case CacheState::M: return CacheStateLabel::Modified;
    }
    return CacheStateLabel::Invalid;
  };
  monitor_->on_cache_state_change(id_, addr, to_label(old_state),
                                  to_label(next_state));
}

void L1Cache::emit_bus_transaction(BusTransaction txn,
                                   tlm::tlm_generic_payload& trans,
                                   sc_core::sc_time& delay) {
  CoherenceHintExtension hint(txn);
  trans.set_extension(&hint);
  mem_socket->b_transport(trans, delay);
  trans.clear_extension(&hint);
}

// ---------------------------------------------------------------
//  Entrada principal del CPU
// ---------------------------------------------------------------
void L1Cache::b_transport_cpu(tlm::tlm_generic_payload& trans,
                               sc_core::sc_time& delay) {
  const uint64_t addr     = align_down(trans.get_address());
  const bool     is_write = trans.is_write();

  if (!payload_fits_line(addr, trans)) {
    trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
    return;
  }

  switch (protocol_) {
    case CoherenceProtocolKind::Msi:
      handle_cpu_msi(addr, is_write, trans, delay);
      break;
    case CoherenceProtocolKind::Firefly:
      handle_cpu_firefly(addr, is_write, trans, delay);
      break;
  }
}

// ---------------------------------------------------------------
//  MSI -- logica CPU
// ---------------------------------------------------------------
void L1Cache::handle_cpu_msi(uint64_t addr, bool is_write,
                              tlm::tlm_generic_payload& trans,
                              sc_core::sc_time& delay) {
  auto       it  = lines_.find(addr);
  const bool hit = (it != lines_.end() &&
                    it->second.state != CacheState::I);

  if (hit) {
    CacheLine& line = it->second;

    if (!is_write) {
      read_from_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "HIT LECTURA   addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  estado=" << cache_state_name(line.state)
          << "  valor=" << format_payload(trans)
          << "  (servido desde cache local, sin bus)\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
      return;
    }

    if (line.state == CacheState::M) {
      write_to_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "HIT ESCRITURA addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  estado=M (exclusivo)  valor=" << format_payload(trans)
          << "  (sin bus, ya somos duenos)\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
      return;
    }

    if (line.state == CacheState::S) {
      write_to_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "UPGRADE S->M  addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  valor a escribir=" << format_payload(trans) << "\n"
          << "               Razon : linea compartida (S), escritura requiere exclusividad\n"
          << "               Accion: BusRdX -> invalida copias en todos los demas caches\n";
      emit_bus_transaction(BusTransaction::BusRdX, trans, delay);
      set_line_state(addr, line, CacheState::M);
      delay += latency_;
      return;
    }
  }

  // MISS
  delay += latency_;
  const BusTransaction bus_txn =
      is_write ? BusTransaction::BusRdX : BusTransaction::BusRd;
  const CacheState new_state =
      is_write ? CacheState::M : CacheState::S;

  std::cout
      << "[t=" << sim_time() << "][L1-" << id_ << "] "
      << "MISS " << (is_write ? "ESCRITURA" : "LECTURA  ")
      << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
      << std::dec << "  I->" << cache_state_name(new_state) << "\n"
      << "               Razon : linea no esta en cache (Invalid)\n"
      << "               Accion: " << (is_write ? "BusRdX" : "BusRd ")
      << " -> busca datos en memoria principal\n";

  emit_bus_transaction(bus_txn, trans, delay);

  CacheLine& line = lines_[addr];
  set_line_state(addr, line, new_state);
  write_to_line(addr, line, trans);

  std::cout
      << "               Resultado: memoria respondio valor="
      << format_payload(trans)
      << "  nuevo estado=" << cache_state_name(new_state) << "\n";

  delay += latency_;
}

// ---------------------------------------------------------------
//  Firefly -- logica CPU
// ---------------------------------------------------------------
void L1Cache::handle_cpu_firefly(uint64_t addr, bool is_write,
                                  tlm::tlm_generic_payload& trans,
                                  sc_core::sc_time& delay) {
  auto       it  = lines_.find(addr);
  const bool hit = (it != lines_.end() &&
                    it->second.state != CacheState::I);

  if (hit) {
    CacheLine& line = it->second;

    if (!is_write) {
      read_from_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "HIT LECTURA   addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  estado=" << cache_state_name(line.state)
          << "  valor=" << format_payload(trans)
          << "  (servido desde cache local, sin bus)\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
      return;
    }

    if (line.state == CacheState::M) {
      write_to_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "HIT ESCRITURA addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  estado=M (exclusivo)  valor=" << format_payload(trans)
          << "  (sin bus)\n";
      delay += latency_;
      trans.set_response_status(tlm::TLM_OK_RESPONSE);
      return;
    }

    if (line.state == CacheState::S) {
      write_to_line(addr, line, trans);
      std::cout
          << "[t=" << sim_time() << "][L1-" << id_ << "] "
          << "ACTUALIZACION S->S addr=0x" << std::hex << std::setw(8)
          << std::setfill('0') << addr << std::dec
          << "  valor difundido=" << format_payload(trans) << "\n"
          << "               Razon : Firefly es write-update; no invalida\n"
          << "               Accion: BusUpd -> todos los caches con copia S\n"
          << "                       reciben el dato nuevo y lo aplican localmente\n";
      emit_bus_transaction(BusTransaction::BusUpd, trans, delay);
      delay += latency_;
      return;
    }
  }

  // MISS
  delay += latency_;
  const BusTransaction bus_txn =
      is_write ? BusTransaction::BusRdX : BusTransaction::BusRd;
  const CacheState new_state =
      is_write ? CacheState::M : CacheState::S;

  std::cout
      << "[t=" << sim_time() << "][L1-" << id_ << "] "
      << "MISS " << (is_write ? "ESCRITURA" : "LECTURA  ")
      << " addr=0x" << std::hex << std::setw(8) << std::setfill('0') << addr
      << std::dec << "  I->" << cache_state_name(new_state) << "\n"
      << "               Razon : linea no esta en cache (Invalid)\n"
      << "               Accion: " << (is_write ? "BusRdX" : "BusRd ")
      << " -> busca datos en memoria principal\n";

  emit_bus_transaction(bus_txn, trans, delay);

  CacheLine& line = lines_[addr];
  set_line_state(addr, line, new_state);
  write_to_line(addr, line, trans);

  std::cout
      << "               Resultado: memoria respondio valor="
      << format_payload(trans)
      << "  nuevo estado=" << cache_state_name(new_state) << "\n";

  delay += latency_;
}

// ---------------------------------------------------------------
//  Snooping -- reaccion a transacciones de otros caches en el bus
// ---------------------------------------------------------------
void L1Cache::snoop(uint64_t addr, BusTransaction type,
                    const tlm::tlm_generic_payload* trans) {
  const uint64_t line_addr = align_down(addr);
  switch (protocol_) {
    case CoherenceProtocolKind::Msi:
      snoop_msi(line_addr, type, trans);
      break;
    case CoherenceProtocolKind::Firefly:
      snoop_firefly(line_addr, type, trans);
      break;
  }
}

// ---------------------------------------------------------------
//  MSI snooping
// ---------------------------------------------------------------
void L1Cache::snoop_msi(uint64_t addr, BusTransaction type,
                        const tlm::tlm_generic_payload* trans) {
  auto it = lines_.find(addr);
  if (it == lines_.end()) return;

  CacheLine& line = it->second;
  switch (type) {
    case BusTransaction::BusRd:
      if (line.state == CacheState::M) {
        // -------------------------------------------------------
        // FIX: cache-to-cache transfer.
        // Este cache tiene el dato modificado. Lo copia al payload
        // del trans ANTES de que memoria responda, para que el
        // requestor reciba el valor correcto (no el stale de mem).
        // -------------------------------------------------------
        if (trans && trans->get_data_ptr() && trans->get_data_length() > 0) {
          const std::size_t offset =
              static_cast<std::size_t>(trans->get_address() - addr);
          const std::size_t len = trans->get_data_length();
          if (offset + len <= kLineSize) {
            std::memcpy(trans->get_data_ptr(),
                        line.data.data() + offset, len);
          }
        }
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusRd   addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  M->S  (otro cache pidio la linea; exclusividad quitada)\n";
        set_line_state(addr, line, CacheState::S);
      }
      break;
    case BusTransaction::BusRdX:
      if (line.state == CacheState::S || line.state == CacheState::M) {
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusRdX  addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  " << cache_state_name(line.state)
            << "->I  (otro cache pide exclusividad; se invalida la copia)\n";
        set_line_state(addr, line, CacheState::I);
      }
      break;
    case BusTransaction::BusUpd:
      // MSI no define BusUpd; ignorado
      break;
  }
}

// ---------------------------------------------------------------
//  Firefly snooping
// ---------------------------------------------------------------
void L1Cache::snoop_firefly(uint64_t addr, BusTransaction type,
                             const tlm::tlm_generic_payload* trans) {
  auto it = lines_.find(addr);
  if (it == lines_.end()) return;

  CacheLine& line = it->second;
  switch (type) {
    case BusTransaction::BusRd:
      if (line.state == CacheState::M) {
        // -------------------------------------------------------
        // FIX: cache-to-cache transfer.
        // Igual que MSI: este cache tiene el dato en M (modificado
        // y no escrito en memoria todavia). Lo provee al requestor
        // copiandolo en el buffer del trans antes de que memoria
        // responda. Sin este paso, memoria devolveria el valor stale.
        // -------------------------------------------------------
        if (trans && trans->get_data_ptr() && trans->get_data_length() > 0) {
          const std::size_t offset =
              static_cast<std::size_t>(trans->get_address() - addr);
          const std::size_t len = trans->get_data_length();
          if (offset + len <= kLineSize) {
            std::memcpy(trans->get_data_ptr(),
                        line.data.data() + offset, len);
          }
        }
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusRd   addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  M->S  (otro cache pidio la linea; valor compartido)\n";
        set_line_state(addr, line, CacheState::S);
      }
      break;
    case BusTransaction::BusRdX:
      if (line.state == CacheState::S || line.state == CacheState::M) {
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusRdX  addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  " << cache_state_name(line.state)
            << "->I  (otro cache pide exclusividad; se invalida la copia)\n";
        set_line_state(addr, line, CacheState::I);
      }
      break;
    case BusTransaction::BusUpd:
      if (line.state == CacheState::S) {
        if (trans) apply_bus_update(addr, line, *trans);
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusUpd  addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  S->S  valor actualizado en cache="
            << format_value(line.data.data(),
                            static_cast<unsigned>(trans ? trans->get_data_length() : 4))
            << "  (dato recibido y aplicado; no es necesario ir a memoria)\n";
      } else if (line.state == CacheState::M) {
        if (trans) apply_bus_update(addr, line, *trans);
        std::cout
            << "[t=" << sim_time() << "][L1-" << id_ << "] "
            << "SNOOP BusUpd  addr=0x" << std::hex << std::setw(8)
            << std::setfill('0') << addr << std::dec
            << "  M->S  valor actualizado en cache="
            << format_value(line.data.data(),
                            static_cast<unsigned>(trans ? trans->get_data_length() : 4))
            << "  (otro cache actualizo; se baja a S y aplicamos dato)\n";
        set_line_state(addr, line, CacheState::S);
      }
      break;
  }
}

}  // namespace mp