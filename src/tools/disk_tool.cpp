#include <kasli/tools/disk_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxDiskUsageRows = 100;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;

void append_bounded(std::string& body, const std::string& text, bool& truncated) {
  if (body.size() + text.size() <= kMaxBodyBytes) {
    body += text;
    return;
  }

  const std::string marker = "truncated=true\n";
  const auto remaining = kMaxBodyBytes - body.size();
  if (remaining > marker.size()) {
    body += text.substr(0, remaining - marker.size());
  } else if (body.size() + marker.size() > kMaxBodyBytes) {
    body.resize(kMaxBodyBytes - marker.size());
  }
  body += marker;
  truncated = true;
}

std::vector<std::string_view> split_spaces(std::string_view value) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start < value.size()) {
    while (start < value.size() && value[start] == ' ') {
      ++start;
    }
    if (start >= value.size()) {
      break;
    }

    const auto separator = value.find(' ', start);
    if (separator == std::string_view::npos) {
      fields.push_back(value.substr(start));
      break;
    }
    fields.push_back(value.substr(start, separator - start));
    start = separator + 1;
  }
  return fields;
}

int octal_digit_value(char value) {
  if (value < '0' || value > '7') {
    return -1;
  }
  return value - '0';
}

std::string decode_mountinfo_value(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '\\' && index + 3 < value.size()) {
      const int first = octal_digit_value(value[index + 1]);
      const int second = octal_digit_value(value[index + 2]);
      const int third = octal_digit_value(value[index + 3]);
      if (first >= 0 && second >= 0 && third >= 0) {
        output.push_back(static_cast<char>((first << 6) | (second << 3) | third));
        index += 3;
        continue;
      }
    }
    output.push_back(value[index]);
  }
  return output;
}

bool is_virtual_filesystem(std::string_view fs_type) {
  static const std::unordered_set<std::string_view> virtual_types = {
      "autofs",     "bpf",        "cgroup",    "cgroup2",   "configfs",
      "debugfs",   "devpts",     "devtmpfs",  "efivarfs",  "fusectl",
      "hugetlbfs", "mqueue",     "proc",      "pstore",    "securityfs",
      "selinuxfs", "sysfs",      "tmpfs",     "tracefs",   "nsfs",
      "ramfs",     "rpc_pipefs", "binfmt_misc"};
  return virtual_types.contains(fs_type);
}

int used_percent(std::uintmax_t capacity_bytes, std::uintmax_t available_bytes) {
  if (capacity_bytes == 0 || available_bytes >= capacity_bytes) {
    return 0;
  }
  const auto used_bytes = capacity_bytes - available_bytes;
  return static_cast<int>((static_cast<long double>(used_bytes) * 100.0L) /
                          static_cast<long double>(capacity_bytes));
}

std::string format_disk_usage_row(const detail::DiskUsageRow& row) {
  return "mount_point=" + core::redact_likely_secret(row.mount_point) +
         " fs_type=" + core::redact_likely_secret(row.fs_type) +
         " source=" + core::redact_likely_secret(row.source) +
         " capacity_bytes=" + std::to_string(row.capacity_bytes) +
         " free_bytes=" + std::to_string(row.free_bytes) +
         " available_bytes=" + std::to_string(row.available_bytes) +
         " used_percent=" + std::to_string(row.used_percent) + '\n';
}

}  // namespace

namespace detail {

std::optional<DiskMountInfo> parse_mountinfo_line(std::string_view line) {
  const auto fields = split_spaces(line);
  if (fields.size() < 10) {
    return std::nullopt;
  }

  auto separator = std::ranges::find(fields, "-");
  if (separator == fields.end()) {
    return std::nullopt;
  }

  const auto separator_index = static_cast<std::size_t>(separator - fields.begin());
  if (separator_index < 6 || separator_index + 3 >= fields.size()) {
    return std::nullopt;
  }

  return DiskMountInfo{
      .mount_point = decode_mountinfo_value(fields[4]),
      .fs_type = decode_mountinfo_value(fields[separator_index + 1]),
      .source = decode_mountinfo_value(fields[separator_index + 2]),
  };
}

bool is_disk_usage_mount_candidate(const DiskMountInfo& mount) {
  if (mount.mount_point.empty() || mount.fs_type.empty()) {
    return false;
  }
  if (is_virtual_filesystem(mount.fs_type)) {
    return false;
  }
  if (mount.mount_point.starts_with("/proc") || mount.mount_point.starts_with("/sys") ||
      mount.mount_point.starts_with("/dev")) {
    return false;
  }
  return true;
}

std::string format_disk_usage_body(const std::vector<DiskUsageRow>& rows, bool has_more_rows) {
  std::string body;
  bool truncated = false;
  append_bounded(body, "disk_mounts_count=" + std::to_string(rows.size()) + '\n', truncated);
  if (rows.empty()) {
    append_bounded(body, "no_disk_mounts=true\n", truncated);
  }

  for (const auto& row : rows) {
    append_bounded(body, format_disk_usage_row(row), truncated);
    if (truncated) {
      return body;
    }
  }

  if (has_more_rows) {
    append_bounded(body, "truncated=true\n", truncated);
  }
  return body;
}

}  // namespace detail

DiskUsageProvider default_disk_usage_provider() {
  return [] {
    std::ifstream input("/proc/self/mountinfo");
    if (!input) {
      return detail::DiskUsageResult{
          .ok = false,
          .error = "mountinfo is not available",
      };
    }

    std::vector<detail::DiskUsageRow> rows;
    bool has_more_rows = false;
    std::string line;
    while (std::getline(input, line)) {
      const auto mount = detail::parse_mountinfo_line(line);
      if (!mount.has_value() || !detail::is_disk_usage_mount_candidate(*mount)) {
        continue;
      }

      std::error_code error;
      const auto space = std::filesystem::space(mount->mount_point, error);
      if (error) {
        continue;
      }

      if (rows.size() < kMaxDiskUsageRows) {
        rows.push_back(detail::DiskUsageRow{
            .mount_point = mount->mount_point,
            .fs_type = mount->fs_type,
            .source = mount->source,
            .capacity_bytes = space.capacity,
            .free_bytes = space.free,
            .available_bytes = space.available,
            .used_percent = used_percent(space.capacity, space.available),
        });
      } else {
        has_more_rows = true;
      }
    }

    std::ranges::sort(rows, [](const auto& left, const auto& right) {
      return left.mount_point < right.mount_point;
    });

    return detail::DiskUsageResult{
        .ok = true,
        .error = "",
        .rows = std::move(rows),
        .has_more_rows = has_more_rows,
    };
  };
}

DiskUsageTool::DiskUsageTool(DiskUsageProvider provider) : provider_(std::move(provider)) {}

std::string DiskUsageTool::name() const {
  return "disk.usage";
}

core::RiskClass DiskUsageTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse DiskUsageTool::call(const core::ToolRequest& request) const {
  auto result = provider_();
  if (!result.ok) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = result.error.empty() ? "disk usage provider failed" : result.error,
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "disk usage listed",
      .evidence = {core::Evidence{
          .id = request.id + ":disk.usage",
          .source = "disk.usage",
          .summary = "bounded mounted filesystem usage",
          .body = detail::format_disk_usage_body(result.rows, result.has_more_rows),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
