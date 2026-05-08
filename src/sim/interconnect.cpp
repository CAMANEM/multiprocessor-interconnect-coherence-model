#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
#include <tlm>
#include "log.hpp"
#include "interconnect.hpp"
#include "l1_cache.hpp"
#include "monitor.hpp"

namespace mp {

namespace {

const char* bus_txn_name(BusTransaction type) {
  switch (type) {
    case BusTransaction::BusRd:
      return "BusRd";
    case BusTransaction::BusRdX:
      return "BusRdX";
    case BusTransaction::BusUpd:
      return "BusUpd";
    case BusTransaction::BusWrBack:
      return "BusWrBack";
  }
  return "Unknown";
}

}  // namespace

/**
 * Constructor: initializes sockets and registers handlers.
 */
Interconnect::Interconnect(sc_core::sc_module_name name, Monitor* monitor,
                           const sc_core::sc_time& latency)
    : sc_module(name),
      tgt0("tgt0"), tgt1("tgt1"), tgt2("tgt2"), tgt3("tgt3"),
      mem_socket("mem_socket"),
      monitor_(monitor),
      latency_(latency),
       bus_mutex_("bus_mutex") {

  tgt0.register_b_transport(this, &Interconnect::b_transport0);
  tgt1.register_b_transport(this, &Interconnect::b_transport1);
  tgt2.register_b_transport(this, &Interconnect::b_transport2);
  tgt3.register_b_transport(this, &Interconnect::b_transport3);

  caches_.fill(nullptr);
}

/**
 * Registers a cache for snooping.
 */
void Interconnect::register_cache(int id, L1Cache* cache) {
  caches_[id] = cache;
}

/**
 * Broadcasts a snoop to all caches except the requester.
 */
void Interconnect::snoop_all(int requester, uint64_t addr, BusTransaction type,
                             const tlm::tlm_generic_payload* trans) {
  for (int i = 0; i < kPorts; ++i) {
    if (i != requester && caches_[i]) {
      caches_[i]->snoop(addr, type, trans);
    }
  }
}

void Interconnect::memory_transport_chunked(int id, BusTransaction metrics_kind,
                                            tlm::tlm_command cmd,
                                            std::uint64_t addr,
                                            unsigned char* data,
                                            unsigned int length,
                                            sc_core::sc_time& delay,
                                            bool record_metrics) {
  if (!data || length == 0) return;

  for (unsigned int offset = 0; offset < length; offset += kBusDataBytes) {
    const unsigned int chunk = std::min(kBusDataBytes, length - offset);

    tlm::tlm_generic_payload beat;
    beat.set_command(cmd);
    beat.set_address(addr + offset);
    beat.set_data_ptr(data + offset);
    beat.set_data_length(chunk);
    beat.set_streaming_width(chunk);
    beat.set_byte_enable_ptr(nullptr);
    beat.set_dmi_allowed(false);
    beat.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    const sc_core::sc_time t_beat_start = delay;
    delay += latency_;
    mem_socket->b_transport(beat, delay);

    if (monitor_ && record_metrics) {
      monitor_->record_bus_transaction(id, chunk, delay - t_beat_start,
                                       metrics_kind);
    }
  }
}

/**
 * Forwards a transaction to memory and records metrics on the Monitor.
 */
void Interconnect::forward(int id, tlm::tlm_generic_payload& trans,
                           sc_core::sc_time& delay) {
  while(true){
    bus_mutex_.lock();
    
    /* Checks for high priority transactions from PEs */
    bool higher_priority_waiting = false;
    if (pending_priority_[id] == 0) {
      for (int i = 0; i < kPorts; ++i) {
        if (i != id && pending_priority_[i] == 1) {
          higher_priority_waiting = true;
          break;
        }
      }
    }

    if(rr_next_ == id && !higher_priority_waiting){
      break;
    }

    if(pending_priority_[id] == 1 && pending_priority_[rr_next_] == 0){
      rr_next_ = id;
      rr_tokens_ = kQuantum;
      break;
    }

    bus_mutex_.unlock();
    wait(SC_ZERO_TIME);
  }

  --rr_tokens_;

  const tlm::tlm_command cmd = trans.get_command();
  const CoherenceHintExtension* hint = trans.get_extension<CoherenceHintExtension>();
  const BusTransaction bus_txn = hint ? hint->transaction
                                      : (trans.is_write() ? BusTransaction::BusRdX
                                                          : BusTransaction::BusRd);

  if (bus_txn == BusTransaction::BusWrBack) {
    memory_transport_chunked(id, bus_txn, tlm::TLM_WRITE_COMMAND,
                             trans.get_address(), trans.get_data_ptr(),
                             trans.get_data_length(), delay, true);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
    return;
  }

  if (Log::enabled(LogLevel::Info)) {
    std::ostringstream os;
    os << "[IC] port=" << id << ' '
       << (cmd == tlm::TLM_READ_COMMAND ? "R" : (cmd == tlm::TLM_WRITE_COMMAND ? "W" : "?"))
       << " txn=" << bus_txn_name(bus_txn)
       << " addr=0x" << std::hex << trans.get_address() << std::dec << " len="
       << trans.get_data_length() << " hop=" << latency_.to_string();
    Log::info(os.str());
  }

  const uint64_t addr = trans.get_address();

  std::vector<unsigned char> pre_snoop_buf;
  if (bus_txn == BusTransaction::BusRd &&
      trans.get_data_ptr() && trans.get_data_length() > 0) {
    pre_snoop_buf.assign(trans.get_data_ptr(),
                         trans.get_data_ptr() + trans.get_data_length());
  }

  snoop_all(id, addr, bus_txn, &trans);

  // Detectar si el snoop modifico el buffer
  bool snoop_provided_data = false;
  if (!pre_snoop_buf.empty()) {
    snoop_provided_data =
        (std::memcmp(pre_snoop_buf.data(),
                     trans.get_data_ptr(),
                     trans.get_data_length()) != 0);
  }

  if (snoop_provided_data) {
    // Requester paga un hop de bus; el WB a RAM corre en paralelo (no aumenta delay del requester).
    delay += latency_;
    sc_core::sc_time wb_delay = delay;
    memory_transport_chunked(id, bus_txn, tlm::TLM_WRITE_COMMAND,
                             trans.get_address(), trans.get_data_ptr(),
                             trans.get_data_length(), wb_delay, false);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
    if (monitor_) {
      monitor_->record_bus_transaction(
          id,
          static_cast<std::uint64_t>(trans.get_data_length()),
          latency_,
          bus_txn);
    }
  } else {
    memory_transport_chunked(id, bus_txn, cmd, trans.get_address(),
                             trans.get_data_ptr(), trans.get_data_length(),
                             delay, true);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  if(rr_tokens_ == 0){
    rr_next_ = (rr_next_ + 1) % kPorts;
    rr_tokens_ = kQuantum;
  }

  pending_priority_[id] = 0;

  bus_mutex_.unlock();
}

void Interconnect::notify_priority(int pe_id, int priority) {
  pending_priority_[pe_id] = priority;
}

/** Port dispatchers */
void Interconnect::b_transport0(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(0, t, d); }
void Interconnect::b_transport1(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(1, t, d); }
void Interconnect::b_transport2(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(2, t, d); }
void Interconnect::b_transport3(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward(3, t, d); }

}  // namespace mp