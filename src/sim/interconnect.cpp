#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
#include <tlm>
#include "log.hpp"
#include "event_log.hpp"
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

thread_local int g_forward_depth = 0;
thread_local bool g_grant_log = false;

struct ForwardDepthGuard {
  ForwardDepthGuard() { ++g_forward_depth; }
  ~ForwardDepthGuard() { --g_forward_depth; }
};

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
      latency_(latency) {

  tgt0.register_b_transport(this, &Interconnect::b_transport0);
  tgt1.register_b_transport(this, &Interconnect::b_transport1);
  tgt2.register_b_transport(this, &Interconnect::b_transport2);
  tgt3.register_b_transport(this, &Interconnect::b_transport3);

  caches_.fill(nullptr);
  pending_.fill(false);
  bus_time_.fill(sc_core::SC_ZERO_TIME);
  SC_THREAD(arbiter);
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
  ForwardDepthGuard guard;
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

  // Registrar transacción IC en EventLog (formal, no en consola)
  std::ostringstream os;
  os << "[IC] port=" << id << ' '
     << (cmd == tlm::TLM_READ_COMMAND ? "R" : (cmd == tlm::TLM_WRITE_COMMAND ? "W" : "?"))
     << " txn=" << bus_txn_name(bus_txn)
     << " addr=0x" << std::hex << trans.get_address() << std::dec << " len="
     << trans.get_data_length() << " hop=" << latency_.to_string();
  if (g_grant_log && g_forward_depth == 1) {
    os << " grant=rr";
  }
  EventLog::record(sc_core::sc_time_stamp(), os.str());

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
}

void Interconnect::forward_arbitrated(int id, tlm::tlm_generic_payload& trans,
                                      sc_core::sc_time& delay) {
  static thread_local bool in_forward = false;
  if (in_forward) {
    forward(id, trans, delay);
    return;
  }

  pending_[id] = true;
  request_ev_.notify(sc_core::SC_ZERO_TIME);

  wait(grant_ev_[id]);

  const sc_core::sc_time before = delay;
  const bool prev_grant = g_grant_log;
  g_grant_log = true;
  in_forward = true;
  forward(id, trans, delay);
  in_forward = false;
  g_grant_log = prev_grant;
  bus_time_[id] = delay - before;

  done_ev_[id].notify(sc_core::SC_ZERO_TIME);
}

void Interconnect::arbiter() {
  while (true) {
    bool any_pending = false;
    for (int pe = 0; pe < kPorts; ++pe) {
      if (pending_[pe]) { any_pending = true; break; }
    }

    if (!any_pending) {
      wait(request_ev_);
      continue;
    }

    int chosen = -1;
    for (int offset = 0; offset < kPorts; ++offset) {
      const int pe = (next_grant_ + offset) % kPorts;
      if (pending_[pe]) { chosen = pe; break; }
    }

    if (chosen < 0) {
      wait(request_ev_);
      continue;
    }

    pending_[chosen] = false;
    
    // Registrar decisión de grant de forma sintetizada
    std::ostringstream grant_msg;
    grant_msg << "[ARBITER RoundRobin] GRANT PE" << chosen 
              << " (pending: ";
    for (int i = 0; i < kPorts; ++i) {
      if (i != chosen && pending_[i]) grant_msg << i << " ";
    }
    grant_msg << ")";
    EventLog::record(sc_core::sc_time_stamp(), grant_msg.str());

    grant_ev_[chosen].notify(sc_core::SC_ZERO_TIME);
    wait(done_ev_[chosen]);

    const sc_core::sc_time bus_time = bus_time_[chosen];
    int next_pos = (chosen + 1) % kPorts;
    next_grant_ = next_pos;
    
    // Registrar finalización con posición siguiente
    std::ostringstream done_msg;
    done_msg << "[ARBITER RoundRobin] PE" << chosen << " completó (" 
             << bus_time.to_string() << ") | próximo: PE" << next_pos;
    EventLog::record(sc_core::sc_time_stamp(), done_msg.str());

    if (bus_time > sc_core::SC_ZERO_TIME) {
      wait(bus_time);
    }
  }
}

/** Port dispatchers */
void Interconnect::b_transport0(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward_arbitrated(0, t, d); }
void Interconnect::b_transport1(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward_arbitrated(1, t, d); }
void Interconnect::b_transport2(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward_arbitrated(2, t, d); }
void Interconnect::b_transport3(tlm::tlm_generic_payload& t, sc_core::sc_time& d) { forward_arbitrated(3, t, d); }

}  // namespace mp