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

class LiveJournalTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

}  // namespace kasli::tools
