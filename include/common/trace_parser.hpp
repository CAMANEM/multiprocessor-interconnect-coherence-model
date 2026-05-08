#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

/** Memory operation encoded in one trace line. */
enum class MemoryOperation : std::uint8_t { Read, Write };

/** One memory access entry parsed from a CE4302 v1 trace file. */
struct TraceEntry {
  std::uint64_t tick;
  int pe_id;
  MemoryOperation op;
  std::uint64_t address;
  std::uint32_t size;
  std::string value;
  int priority{0};
};

/**
 * CE4302 v1 trace container (lines: tick pe_id R|W address [size]; '#' starts a comment).
 */
class TraceFile {
public:
  /**
   * Loads and parses a trace file from disk.
   *
   * @param path path to the .trace file
   * @param error_out human-readable message on load or format failure
   * @return true on success; false with error_out set otherwise
   */
  bool load(const std::string& path, std::string& error_out);

  /**
   * @return reference to all entries sorted by tick, then pe_id and address
   */
  const std::vector<TraceEntry>& all_entries() const { return entries_; }

  /**
   * Filters entries for a single PE while preserving global tick order.
   *
   * @param pe_id processing element id (0..3 in the current topology)
   * @return copy of entries for that PE
   */
  std::vector<TraceEntry> entries_for_pe(int pe_id) const;

private:
  std::vector<TraceEntry> entries_;
};

}  // namespace mp
