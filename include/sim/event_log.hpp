#pragma once

#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>
#include <algorithm>
#include <systemc>

namespace mp {

class EventLog {
public:
  struct Event {
    double      time_ns;
    int         seq;
    std::string text;
  };

  static void record(const sc_core::sc_time& t, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(Event{ t.to_double(), seq_++, text });
  }

  static void dump(std::ostream& os) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::stable_sort(events_.begin(), events_.end(),
        [](const Event& a, const Event& b) {
          if (a.time_ns != b.time_ns) return a.time_ns < b.time_ns;
          return a.seq < b.seq;
        });
    for (const auto& e : events_) {
      os << e.text;
      if (e.text.empty() || e.text.back() != '\n') os << '\n';
    }
  }

  static void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    seq_ = 0;
  }

private:
  static std::vector<Event> events_;
  static std::mutex         mutex_;
  static int                seq_;
};

}  // namespace mp