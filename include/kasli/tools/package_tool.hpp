#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kasli::tools {

namespace detail {

struct PackageChangeRow {
  std::string timestamp;
  std::string action;
  std::string package;
  std::string source;
};

std::optional<PackageChangeRow> parse_package_change_log_line(std::string_view line,
                                                              const std::string& source);
std::string format_package_recent_changes_body(const std::vector<PackageChangeRow>& rows,
                                               bool has_more_rows);

}  // namespace detail

std::vector<std::filesystem::path> default_package_recent_change_log_paths();

class PackageRecentChangesTool final : public Tool {
 public:
  explicit PackageRecentChangesTool(
      std::vector<std::filesystem::path> log_paths = default_package_recent_change_log_paths());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  std::vector<std::filesystem::path> log_paths_;
};

}  // namespace kasli::tools
