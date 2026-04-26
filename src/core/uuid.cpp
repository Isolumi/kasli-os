#include <kasli/core/uuid.hpp>

#include <atomic>
#include <chrono>
#include <sstream>

namespace kasli::core {

std::string make_event_id() {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();
  std::ostringstream output;
  output << "event-" << now << "-" << counter.fetch_add(1);
  return output.str();
}

}  // namespace kasli::core
