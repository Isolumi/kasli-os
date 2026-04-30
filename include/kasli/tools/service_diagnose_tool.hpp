#pragma once

#include <kasli/tools/tool.hpp>

namespace kasli::tools {

class ServiceDiagnoseTool final : public Tool {
 public:
  ServiceDiagnoseTool(const Tool& status_tool, const Tool& journal_tool);

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  const Tool& status_tool_;
  const Tool& journal_tool_;
};

}  // namespace kasli::tools
