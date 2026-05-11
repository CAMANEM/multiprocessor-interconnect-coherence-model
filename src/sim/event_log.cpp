#include "event_log.hpp"

namespace mp {
std::vector<EventLog::Event> EventLog::events_;
std::mutex                   EventLog::mutex_;
int                          EventLog::seq_ = 0;
}  // namespace mp