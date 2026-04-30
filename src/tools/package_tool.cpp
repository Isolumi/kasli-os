#include <kasli/tools/package_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxPackageChanges = 100;
constexpr std::size_t kMaxCollectedChanges = 2000;
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

std::string first_token(std::string_view value) {
  const auto separator = value.find(' ');
  if (separator == std::string_view::npos) {
    return std::string(value);
  }
  return std::string(value.substr(0, separator));
}

std::optional<std::string> rpm_callback_action(std::string_view line) {
  constexpr std::string_view prefix = " INFO RPM callback ";
  const auto prefix_position = line.find(prefix);
  if (prefix_position == std::string_view::npos) {
    return std::nullopt;
  }

  const auto action_start = prefix_position + prefix.size();
  constexpr std::string_view start_marker = " start \"";
  const auto action_end = line.find(start_marker, action_start);
  if (action_end == std::string_view::npos) {
    return std::nullopt;
  }

  const std::string action(line.substr(action_start, action_end - action_start));
  if (action == "install" || action == "erase" || action == "upgrade" ||
      action == "downgrade" || action == "reinstall") {
    return action;
  }
  return std::nullopt;
}

std::string dnf4_action_from_verb(std::string_view verb) {
  if (verb == "Installed") return "install";
  if (verb == "Upgraded" || verb == "Updated") return "upgrade";
  if (verb == "Erased" || verb == "Removed") return "remove";
  if (verb == "Downgraded") return "downgrade";
  if (verb == "Reinstalled") return "reinstall";
  return "";
}

}  // namespace

namespace detail {

std::optional<PackageChangeRow> parse_package_change_log_line(std::string_view line,
                                                              const std::string& source) {
  if (const auto action = rpm_callback_action(line)) {
    constexpr std::string_view start_marker = " start \"";
    const auto package_start = line.find(start_marker);
    if (package_start == std::string_view::npos) {
      return std::nullopt;
    }

    const auto value_start = package_start + start_marker.size();
    const auto value_end = line.find('"', value_start);
    if (value_end == std::string_view::npos || value_end == value_start) {
      return std::nullopt;
    }

    return PackageChangeRow{
        .timestamp = first_token(line),
        .action = *action,
        .package = std::string(line.substr(value_start, value_end - value_start)),
        .source = source,
    };
  }

  for (const std::string_view verb :
       {"Installed", "Upgraded", "Updated", "Erased", "Removed", "Downgraded", "Reinstalled"}) {
    const std::string marker = " INFO " + std::string(verb) + ": ";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) {
      continue;
    }

    const auto package_start = position + marker.size();
    if (package_start >= line.size()) {
      return std::nullopt;
    }

    return PackageChangeRow{
        .timestamp = first_token(line),
        .action = dnf4_action_from_verb(verb),
        .package = std::string(line.substr(package_start)),
        .source = source,
    };
  }

  return std::nullopt;
}

std::string format_package_recent_changes_body(const std::vector<PackageChangeRow>& rows,
                                               bool has_more_rows) {
  std::string body;
  bool truncated = false;
  append_bounded(body, "package_changes_count=" + std::to_string(rows.size()) + '\n', truncated);
  if (rows.empty()) {
    append_bounded(body, "no_package_changes=true\n", truncated);
  }

  for (const auto& row : rows) {
    append_bounded(body,
                   "timestamp=" + core::redact_likely_secret(row.timestamp) +
                       " action=" + core::redact_likely_secret(row.action) +
                       " package=" + core::redact_likely_secret(row.package) +
                       " source=" + core::redact_likely_secret(row.source) + '\n',
                   truncated);
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

std::vector<std::filesystem::path> default_package_recent_change_log_paths() {
  return {
      "/var/log/dnf5.log",
      "/var/log/dnf5.log.1",
      "/var/log/dnf5.log.2",
      "/var/log/dnf5.log.3",
      "/var/log/dnf5.log.4",
      "/var/log/dnf5.log.5",
      "/var/log/dnf.rpm.log",
      "/var/log/dnf.rpm.log.1",
      "/var/log/dnf.log",
      "/var/log/dnf.log.1",
      "/var/log/yum.log",
      "/var/log/yum.log.1",
  };
}

PackageRecentChangesTool::PackageRecentChangesTool(std::vector<std::filesystem::path> log_paths)
    : log_paths_(std::move(log_paths)) {}

std::string PackageRecentChangesTool::name() const {
  return "packages.recent_changes";
}

core::RiskClass PackageRecentChangesTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse PackageRecentChangesTool::call(const core::ToolRequest& request) const {
  std::vector<detail::PackageChangeRow> rows;
  bool saw_readable_log = false;
  bool has_more_rows = false;

  for (const auto& path : log_paths_) {
    if (!std::filesystem::is_regular_file(path)) {
      continue;
    }

    std::ifstream input(path);
    if (!input) {
      continue;
    }
    saw_readable_log = true;

    std::string line;
    while (std::getline(input, line)) {
      auto row = detail::parse_package_change_log_line(line, path.string());
      if (!row.has_value()) {
        continue;
      }

      if (rows.size() < kMaxCollectedChanges) {
        rows.push_back(std::move(*row));
      } else {
        has_more_rows = true;
      }
    }
  }

  if (!saw_readable_log) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "package history logs not found",
        .evidence = {},
    };
  }

  std::ranges::sort(rows, [](const auto& left, const auto& right) {
    return left.timestamp > right.timestamp;
  });

  if (rows.size() > kMaxPackageChanges) {
    rows.resize(kMaxPackageChanges);
    has_more_rows = true;
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "recent package changes listed",
      .evidence = {core::Evidence{
          .id = request.id + ":packages.recent_changes",
          .source = "packages.recent_changes",
          .summary = "bounded recent package changes",
          .body = detail::format_package_recent_changes_body(rows, has_more_rows),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
