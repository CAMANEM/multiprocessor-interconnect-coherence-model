#pragma once

#include <cstdint>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>

namespace mp {

class Monitor;
class L1Cache;

/** Coherence transactions supported by the interconnect. */
enum class BusTransaction : std::uint8_t { BusRd, BusRdX, BusUpd };

/**
 * Optional payload extension used by L1 to hint which coherence transaction
 * should be broadcast by the interconnect.
 */
struct CoherenceHintExtension : tlm::tlm_extension<CoherenceHintExtension> {
  BusTransaction transaction{BusTransaction::BusRd};

  explicit CoherenceHintExtension(BusTransaction t = BusTransaction::BusRd)
      : transaction(t) {}

  tlm::tlm_extension_base* clone() const override {
    return new CoherenceHintExtension(transaction);
  }

  void copy_from(const tlm::tlm_extension_base& ext) override {
    transaction = static_cast<const CoherenceHintExtension&>(ext).transaction;
  }
};

/**
 * Cache-coherent interconnect (bus-based).
 *
 * Responsibilities:
 *  - Forward transactions from L1 caches to shared memory
 *  - Broadcast coherence events to all other caches (snooping)
 */
class Interconnect : public sc_core::sc_module {
public:
  static constexpr int kPorts = 4;

  tlm_utils::simple_target_socket<Interconnect> tgt0;
  tlm_utils::simple_target_socket<Interconnect> tgt1;
  tlm_utils::simple_target_socket<Interconnect> tgt2;
  tlm_utils::simple_target_socket<Interconnect> tgt3;

  tlm_utils::simple_initiator_socket<Interconnect> mem_socket;

  Interconnect(sc_core::sc_module_name name, Monitor* monitor,
               const sc_core::sc_time& latency);

  void register_cache(int id, L1Cache* cache);

private:
  void b_transport0(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport1(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport2(tlm::tlm_generic_payload&, sc_core::sc_time&);
  void b_transport3(tlm::tlm_generic_payload&, sc_core::sc_time&);

  void forward(int id, tlm::tlm_generic_payload&, sc_core::sc_time&);
  void snoop_all(int requester, uint64_t addr, BusTransaction type,
                 const tlm::tlm_generic_payload* trans);

  std::array<L1Cache*, kPorts> caches_;

  Monitor*          monitor_{ nullptr };
  sc_core::sc_time  latency_;
};

}  // namespace mp