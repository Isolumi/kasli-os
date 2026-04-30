#pragma once

#include <kasli/tools/tool.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kasli::tools {

namespace detail {

struct DiskMountInfo {
  std::string mount_point;
  std::string fs_type;
  std::string source;
};

struct DiskUsageRow {
  std::string mount_point;
  std::string fs_type;
  std::string source;
  std::uintmax_t capacity_bytes = 0;
  std::uintmax_t free_bytes = 0;
  std::uintmax_t available_bytes = 0;
  int used_percent = 0;
};

struct DiskUsageResult {
  bool ok = false;
  std::string error;
  std::vector<DiskUsageRow> rows;
  bool has_more_rows = false;
};

std::optional<DiskMountInfo> parse_mountinfo_line(std::string_view line);
bool is_disk_usage_mount_candidate(const DiskMountInfo& mount);
std::string format_disk_usage_body(const std::vector<DiskUsageRow>& rows, bool has_more_rows);

}  // namespace detail

using DiskUsageProvider = std::function<detail::DiskUsageResult()>;

DiskUsageProvider default_disk_usage_provider();

class DiskUsageTool final : public Tool {
 public:
  explicit DiskUsageTool(DiskUsageProvider provider = default_disk_usage_provider());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  DiskUsageProvider provider_;
};

}  // namespace kasli::tools
