#pragma once

#include <kasli/core/types.hpp>

#include <string>

namespace kasli::tools {

class Tool {
 public:
  virtual ~Tool() = default;

  virtual std::string name() const = 0;
  virtual core::RiskClass risk() const = 0;
  virtual core::ToolResponse call(const core::ToolRequest& request) const = 0;
};

}  // namespace kasli::tools
