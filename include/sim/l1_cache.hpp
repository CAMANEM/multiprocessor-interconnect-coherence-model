#pragma once

#include <array>
#include <systemc>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <unordered_map>

#include "interconnect.hpp"

namespace mp {

class Monitor;

enum class CoherenceProtocolKind { Msi, Firefly };

enum class CacheState { I, S, M };

inline constexpr std::size_t kCacheLineBytes = 64;

struct CacheLine {
  CacheState state{ CacheState::I };
  std::array<unsigned char, kCacheLineBytes> data{};
};

class L1Cache : public sc_core::sc_module {
public:
  tlm_utils::simple_target_socket<L1Cache>    cpu_socket;
  tlm_utils::simple_initiator_socket<L1Cache> mem_socket;

  L1Cache(sc_core::sc_module_name name, int id, Monitor* monitor,
          CoherenceProtocolKind protocol, const sc_core::sc_time& latency);

  void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
  void snoop(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);

private:
  uint64_t align_down(uint64_t addr);
  bool payload_fits_line(uint64_t line_addr, const tlm::tlm_generic_payload& trans) const;
  void read_from_line(uint64_t line_addr, const CacheLine& line,
                      tlm::tlm_generic_payload& trans) const;
  void write_to_line(uint64_t line_addr, CacheLine& line,
                     const tlm::tlm_generic_payload& trans);
  void apply_bus_update(uint64_t line_addr, CacheLine& line,
                        const tlm::tlm_generic_payload& trans);
  void emit_bus_transaction(BusTransaction txn, tlm::tlm_generic_payload& trans,
                            sc_core::sc_time& delay);

  void handle_cpu_msi(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                      sc_core::sc_time& delay);
  void handle_cpu_firefly(uint64_t addr, bool is_write, tlm::tlm_generic_payload& trans,
                          sc_core::sc_time& delay);
  void snoop_msi(uint64_t addr, BusTransaction type, const tlm::tlm_generic_payload* trans);
  void snoop_firefly(uint64_t addr, BusTransaction type,
                     const tlm::tlm_generic_payload* trans);

  void writeback_line_to_mem(uint64_t line_addr, const CacheLine& line,
                              sc_core::sc_time& delay);

  void set_line_state(uint64_t addr, CacheLine& line, CacheState next_state);
  static const char* cache_state_name(CacheState state);

  int                      id_;
  Monitor*                 monitor_;
  CoherenceProtocolKind    protocol_;
  sc_core::sc_time         latency_;

  static constexpr uint64_t kLineSize = kCacheLineBytes;

  std::unordered_map<uint64_t, CacheLine> lines_;
};

}  // namespace mp