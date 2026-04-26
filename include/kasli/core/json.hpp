#pragma once

#include <string>

namespace kasli::core {

std::string redact_likely_secret(const std::string& input);

}  // namespace kasli::core
