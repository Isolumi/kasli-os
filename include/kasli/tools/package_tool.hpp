#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>
#include <functional>
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

struct PackageListRow {
  std::string name;
  std::string epoch;
  std::string version;
  std::string release;
  std::string arch;
  std::string install_time;
  std::string source;
};

struct PackageCommandResult {
  bool ok = false;
  int exit_code = -1;
  std::string output;
  std::string error;
};

std::optional<PackageChangeRow> parse_package_change_log_line(std::string_view line,
                                                              const std::string& source);
std::optional<PackageListRow> parse_rpm_package_list_line(std::string_view line,
                                                          const std::string& source);
std::string format_package_recent_changes_body(const std::vector<PackageChangeRow>& rows,
                                               bool has_more_rows);
std::string format_package_list_body(const std::vector<PackageListRow>& rows,
                                     bool has_more_rows);

}  // namespace detail

using PackageListRunner = std::function<detail::PackageCommandResult()>;

std::vector<std::filesystem::path> default_package_recent_change_log_paths();
PackageListRunner default_package_list_runner();

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

class PackageListTool final : public Tool {
 public:
  explicit PackageListTool(PackageListRunner runner = default_package_list_runner());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  PackageListRunner runner_;
};

}  // namespace kasli::tools
