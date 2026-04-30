#include <kasli/tools/package_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxPackageChanges = 100;
constexpr std::size_t kMaxCollectedChanges = 2000;
constexpr std::size_t kMaxPackageListRows = 500;
constexpr std::size_t kMaxPackageCommandOutputBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr auto kPackageCommandTimeout = std::chrono::seconds(5);

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

std::vector<std::string_view> split_tab_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto separator = line.find('\t', start);
    if (separator == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, separator - start));
    start = separator + 1;
  }
  return fields;
}

std::string format_package_list_row(const detail::PackageListRow& row) {
  return "name=" + core::redact_likely_secret(row.name) +
         " epoch=" + core::redact_likely_secret(row.epoch) +
         " version=" + core::redact_likely_secret(row.version) +
         " release=" + core::redact_likely_secret(row.release) +
         " arch=" + core::redact_likely_secret(row.arch) +
         " install_time=" + core::redact_likely_secret(row.install_time) +
         " source=" + core::redact_likely_secret(row.source) + '\n';
}

std::optional<std::filesystem::path> find_rpm_executable() {
  for (const auto& candidate : std::array{"/usr/bin/rpm", "/bin/rpm", "/usr/local/bin/rpm"}) {
    if (std::filesystem::exists(candidate) && ::access(candidate, X_OK) == 0) {
      return std::filesystem::path(candidate);
    }
  }
  return std::nullopt;
}

detail::PackageCommandResult run_fixed_command(const std::filesystem::path& executable,
                                               const std::vector<std::string>& arguments) {
  int output_pipe[2] = {-1, -1};
  if (::pipe(output_pipe) != 0) {
    return detail::PackageCommandResult{
        .ok = false,
        .exit_code = -1,
        .error = "failed to create package inventory pipe",
    };
  }

  const std::string executable_string = executable.string();
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 2);
  argv.push_back(const_cast<char*>(executable_string.c_str()));
  for (const auto& argument : arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t child = ::fork();
  if (child < 0) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    return detail::PackageCommandResult{
        .ok = false,
        .exit_code = -1,
        .error = "failed to start package inventory command",
    };
  }

  if (child == 0) {
    ::close(output_pipe[0]);
    if (::dup2(output_pipe[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    ::close(output_pipe[1]);

    const int dev_null = ::open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      ::dup2(dev_null, STDERR_FILENO);
      ::close(dev_null);
    }

    ::execv(executable_string.c_str(), argv.data());
    _exit(127);
  }

  ::close(output_pipe[1]);
  const int flags = ::fcntl(output_pipe[0], F_GETFL, 0);
  if (flags >= 0) {
    ::fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
  }

  std::string output;
  bool pipe_closed = false;
  bool child_done = false;
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + kPackageCommandTimeout;

  while (!(pipe_closed && child_done)) {
    char buffer[8192];
    while (true) {
      const ssize_t bytes_read = ::read(output_pipe[0], buffer, sizeof(buffer));
      if (bytes_read > 0) {
        if (output.size() + static_cast<std::size_t>(bytes_read) > kMaxPackageCommandOutputBytes) {
          ::kill(child, SIGKILL);
          ::close(output_pipe[0]);
          ::waitpid(child, &status, 0);
          return detail::PackageCommandResult{
              .ok = false,
              .exit_code = -1,
              .error = "package inventory command output exceeded limit",
          };
        }
        output.append(buffer, static_cast<std::size_t>(bytes_read));
        continue;
      }

      if (bytes_read == 0) {
        pipe_closed = true;
        break;
      }

      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }

      ::kill(child, SIGKILL);
      ::close(output_pipe[0]);
      ::waitpid(child, &status, 0);
      return detail::PackageCommandResult{
          .ok = false,
          .exit_code = -1,
          .error = "failed to read package inventory command output",
      };
    }

    if (!child_done) {
      const pid_t wait_result = ::waitpid(child, &status, WNOHANG);
      if (wait_result == child) {
        child_done = true;
      } else if (wait_result < 0 && errno != EINTR) {
        ::close(output_pipe[0]);
        return detail::PackageCommandResult{
            .ok = false,
            .exit_code = -1,
            .error = "failed to wait for package inventory command",
        };
      }
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      ::kill(child, SIGKILL);
      ::close(output_pipe[0]);
      ::waitpid(child, &status, 0);
      return detail::PackageCommandResult{
          .ok = false,
          .exit_code = -1,
          .error = "package inventory command timed out",
      };
    }

    if (!(pipe_closed && child_done)) {
      pollfd descriptor{
          .fd = output_pipe[0],
          .events = POLLIN,
          .revents = 0,
      };
      ::poll(&descriptor, 1, 50);
    }
  }

  ::close(output_pipe[0]);

  int exit_code = -1;
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exit_code = 128 + WTERMSIG(status);
  }

  if (exit_code != 0) {
    return detail::PackageCommandResult{
        .ok = false,
        .exit_code = exit_code,
        .error = "package inventory command failed",
    };
  }

  return detail::PackageCommandResult{
      .ok = true,
      .exit_code = exit_code,
      .output = std::move(output),
      .error = "",
  };
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

std::optional<PackageListRow> parse_rpm_package_list_line(std::string_view line,
                                                          const std::string& source) {
  const auto fields = split_tab_fields(line);
  if (fields.size() != 6) {
    return std::nullopt;
  }

  for (const auto field : fields) {
    if (field.empty()) {
      return std::nullopt;
    }
  }

  return PackageListRow{
      .name = std::string(fields[0]),
      .epoch = std::string(fields[1]),
      .version = std::string(fields[2]),
      .release = std::string(fields[3]),
      .arch = std::string(fields[4]),
      .install_time = std::string(fields[5]),
      .source = source,
  };
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

std::string format_package_list_body(const std::vector<PackageListRow>& rows, bool has_more_rows) {
  std::string body;
  bool truncated = false;
  append_bounded(body, "packages_count=" + std::to_string(rows.size()) + '\n', truncated);
  if (rows.empty()) {
    append_bounded(body, "no_packages=true\n", truncated);
  }

  for (const auto& row : rows) {
    append_bounded(body, format_package_list_row(row), truncated);
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

PackageListRunner default_package_list_runner() {
  return [] {
    const auto rpm = find_rpm_executable();
    if (!rpm.has_value()) {
      return detail::PackageCommandResult{
          .ok = false,
          .exit_code = 127,
          .error = "rpm executable not found",
      };
    }

    return run_fixed_command(
        *rpm,
        {"-qa", "--qf", "%{NAME}\t%{EPOCHNUM}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{INSTALLTIME}\n"});
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

PackageListTool::PackageListTool(PackageListRunner runner) : runner_(std::move(runner)) {}

std::string PackageListTool::name() const {
  return "packages.list";
}

core::RiskClass PackageListTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse PackageListTool::call(const core::ToolRequest& request) const {
  const auto command_result = runner_();
  if (!command_result.ok) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message =
            command_result.error.empty() ? "package inventory command failed" : command_result.error,
        .evidence = {},
    };
  }

  std::vector<detail::PackageListRow> rows;
  bool has_more_rows = false;
  std::istringstream input(command_result.output);
  std::string line;
  while (std::getline(input, line)) {
    auto row = detail::parse_rpm_package_list_line(line, "rpmdb");
    if (!row.has_value()) {
      continue;
    }

    if (rows.size() < kMaxPackageListRows) {
      rows.push_back(std::move(*row));
    } else {
      has_more_rows = true;
    }
  }

  std::ranges::sort(rows, [](const auto& left, const auto& right) {
    if (left.name == right.name) {
      return left.arch < right.arch;
    }
    return left.name < right.name;
  });

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "installed packages listed",
      .evidence = {core::Evidence{
          .id = request.id + ":packages.list",
          .source = "packages.list",
          .summary = "bounded installed package list",
          .body = detail::format_package_list_body(rows, has_more_rows),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
