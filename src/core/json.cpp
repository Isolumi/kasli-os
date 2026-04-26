#include <kasli/core/json.hpp>

#include <regex>

namespace kasli::core {

std::string redact_likely_secret(const std::string& input) {
  static const std::regex assignment_pattern(
      R"((password|passwd|token|secret|api[_-]?key)=([^ \n\r\t]+))",
      std::regex_constants::icase);
  return std::regex_replace(input, assignment_pattern, "$1=[REDACTED]");
}

}  // namespace kasli::core
