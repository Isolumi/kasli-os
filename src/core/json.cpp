#include <kasli/core/json.hpp>

#include <regex>

namespace kasli::core {

std::string redact_likely_secret(const std::string& input) {
  static const std::regex quoted_assignment_pattern(
      R"regex(((password|passwd|token|secret|api[_-]?key)\s*=\s*)"[^"]*")regex",
      std::regex_constants::icase);
  static const std::regex assignment_pattern(
      R"(((password|passwd|token|secret|api[_-]?key)\s*=\s*)([^" \n\r\t]+))",
      std::regex_constants::icase);
  static const std::regex colon_pattern(
      R"(((password|passwd|token|secret|api[_-]?key)\s*:\s*)([^ \n\r\t]+))",
      std::regex_constants::icase);
  static const std::regex bearer_pattern(R"((Authorization\s*:\s*Bearer\s+)([^ \n\r\t]+))",
                                         std::regex_constants::icase);

  auto redacted = std::regex_replace(input, quoted_assignment_pattern, "$1\"[REDACTED]\"");
  redacted = std::regex_replace(redacted, bearer_pattern, "$1[REDACTED]");
  redacted = std::regex_replace(redacted, colon_pattern, "$1[REDACTED]");
  return std::regex_replace(redacted, assignment_pattern, "$1[REDACTED]");
}

}  // namespace kasli::core
