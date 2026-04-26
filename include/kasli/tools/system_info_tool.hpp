#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>

namespace kasli::tools {

class SystemInfoTool final : public Tool {
 public:
  explicit SystemInfoTool(std::filesystem::path os_release_path = "/etc/os-release");

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  std::filesystem::path os_release_path_;
};

}  // namespace kasli::tools
