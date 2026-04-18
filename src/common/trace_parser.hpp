#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

enum class MemoryOperation : std::uint8_t { Read, Write };

struct TraceEntry {
  std::uint64_t tick;
  int pe_id;
  MemoryOperation op;
  std::uint64_t address;
  std::uint32_t size;
};

// CE4302 trace v1 (one logical time unit per tick; default 1 ns in simulator)
// Lines:
//   # optional comments / metadata
//   tick pe_id R|W address_hex [size]
// Example: 10 0 W 0x1000 4
class TraceFile {
public:
  bool load(const std::string& path, std::string& error_out);

  const std::vector<TraceEntry>& all_entries() const { return entries_; }

  std::vector<TraceEntry> entries_for_pe(int pe_id) const;

private:
  std::vector<TraceEntry> entries_;
};

}  // namespace mp
