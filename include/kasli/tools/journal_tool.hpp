#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>

namespace kasli::tools {

class JournalFixtureTool final : public Tool {
 public:
  explicit JournalFixtureTool(std::filesystem::path fixture_path);

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  std::filesystem::path fixture_path_;
};

}  // namespace kasli::tools
