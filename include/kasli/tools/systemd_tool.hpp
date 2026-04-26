#pragma once

#include <kasli/tools/tool.hpp>

namespace kasli::tools {

class SystemdUnitsTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

class SystemdUnitStatusTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

}  // namespace kasli::tools
