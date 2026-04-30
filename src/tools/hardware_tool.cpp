#include <kasli/tools/hardware_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <utility>
#include <vector>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxGpuRows = 16;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr std::size_t kMaxHardwareInputBytes = 64 * 1024;

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

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                           value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    value.erase(0, start);
  }
  return value;
}

std::optional<std::pair<std::string, std::string>> parse_key_value_line(std::string_view line) {
  const auto separator = line.find(':');
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }
  return std::pair{
      trim(std::string(line.substr(0, separator))),
      trim(std::string(line.substr(separator + 1))),
  };
}

std::size_t parse_size(std::string_view value) {
  try {
    return static_cast<std::size_t>(std::stoull(std::string(value)));
  } catch (...) {
    return 0;
  }
}

std::uintmax_t parse_kib_value(std::string_view value) {
  std::size_t end = 0;
  while (end < value.size() && value[end] >= '0' && value[end] <= '9') {
    ++end;
  }
  if (end == 0) {
    return 0;
  }
  try {
    return static_cast<std::uintmax_t>(std::stoull(std::string(value.substr(0, end)))) * 1024ULL;
  } catch (...) {
    return 0;
  }
}

std::string unknown_if_empty(const std::string& value) {
  return value.empty() ? "unknown" : value;
}

std::string read_text_file(const std::filesystem::path& path, std::string fallback = "unknown") {
  std::ifstream input(path);
  if (!input) {
    return fallback;
  }
  std::string value;
  std::getline(input, value);
  value = trim(value);
  return value.empty() ? fallback : value;
}

std::string read_file_contents(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }

  std::string content(kMaxHardwareInputBytes + 1, '\0');
  input.read(content.data(), static_cast<std::streamsize>(content.size()));
  content.resize(static_cast<std::size_t>(input.gcount()));
  return detail::truncate_hardware_input(std::move(content));
}

bool is_drm_card_name(const std::string& name) {
  if (!name.starts_with("card") || name.size() == 4) {
    return false;
  }
  return std::ranges::all_of(name.begin() + 4, name.end(), [](char value) {
    return value >= '0' && value <= '9';
  });
}

std::string read_driver_name(const std::filesystem::path& device_path) {
  std::error_code error;
  const auto driver_path = std::filesystem::read_symlink(device_path / "driver", error);
  if (error || driver_path.empty()) {
    return "unknown";
  }
  return driver_path.filename().string();
}

std::string format_gpu_row(const detail::GpuDeviceRow& row) {
  return "gpu=name=" + core::redact_likely_secret(row.name) +
         " vendor_id=" + core::redact_likely_secret(unknown_if_empty(row.vendor_id)) +
         " device_id=" + core::redact_likely_secret(unknown_if_empty(row.device_id)) +
         " class=" + core::redact_likely_secret(unknown_if_empty(row.class_id)) +
         " driver=" + core::redact_likely_secret(unknown_if_empty(row.driver)) +
         " source=" + core::redact_likely_secret(unknown_if_empty(row.source)) + '\n';
}

void finalize_cpu_block(bool has_processor,
                        const std::string& physical_id,
                        const std::string& core_id,
                        std::size_t cpu_cores,
                        std::size_t& logical_processors,
                        std::size_t& fallback_cores,
                        std::map<std::string, std::size_t>& socket_core_counts,
                        std::set<std::string>& physical_core_ids) {
  if (!has_processor) {
    return;
  }

  ++logical_processors;
  if (!physical_id.empty() && !core_id.empty()) {
    physical_core_ids.insert(physical_id + ":" + core_id);
  } else if (!physical_id.empty() && cpu_cores > 0) {
    socket_core_counts[physical_id] = std::max(socket_core_counts[physical_id], cpu_cores);
  }
  fallback_cores = std::max(fallback_cores, cpu_cores);
}

}  // namespace

namespace detail {

CpuSummary parse_cpuinfo_summary(std::string_view content) {
  CpuSummary summary;
  std::set<std::string> physical_core_ids;
  std::map<std::string, std::size_t> socket_core_counts;
  std::size_t fallback_cores = 0;
  std::size_t logical_processors = 0;

  bool has_processor = false;
  std::string physical_id;
  std::string core_id;
  std::size_t cpu_cores = 0;

  std::size_t start = 0;
  while (start <= content.size()) {
    const auto end = content.find('\n', start);
    const auto line = end == std::string_view::npos ? content.substr(start)
                                                    : content.substr(start, end - start);
    if (trim(std::string(line)).empty()) {
      finalize_cpu_block(has_processor,
                         physical_id,
                         core_id,
                         cpu_cores,
                         logical_processors,
                         fallback_cores,
                         socket_core_counts,
                         physical_core_ids);
      has_processor = false;
      physical_id.clear();
      core_id.clear();
      cpu_cores = 0;
    } else if (const auto item = parse_key_value_line(line)) {
      const auto& key = item->first;
      const auto& value = item->second;
      if (key == "processor") {
        has_processor = true;
      } else if (key == "vendor_id" && summary.vendor_id.empty()) {
        summary.vendor_id = value;
      } else if (key == "model name" && summary.model_name.empty()) {
        summary.model_name = value;
      } else if (key == "physical id") {
        physical_id = value;
      } else if (key == "core id") {
        core_id = value;
      } else if (key == "cpu cores") {
        cpu_cores = parse_size(value);
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  finalize_cpu_block(has_processor,
                     physical_id,
                     core_id,
                     cpu_cores,
                     logical_processors,
                     fallback_cores,
                     socket_core_counts,
                     physical_core_ids);

  summary.logical_processors = logical_processors;
  if (!physical_core_ids.empty()) {
    summary.physical_cores = physical_core_ids.size();
  } else if (!socket_core_counts.empty()) {
    for (const auto& [socket_id, cores] : socket_core_counts) {
      (void)socket_id;
      summary.physical_cores += cores;
    }
  } else if (fallback_cores > 0) {
    summary.physical_cores = fallback_cores;
  } else {
    summary.physical_cores = logical_processors;
  }
  return summary;
}

MemorySummary parse_meminfo_summary(std::string_view content) {
  MemorySummary summary;

  std::size_t start = 0;
  while (start <= content.size()) {
    const auto end = content.find('\n', start);
    const auto line = end == std::string_view::npos ? content.substr(start)
                                                    : content.substr(start, end - start);
    if (const auto item = parse_key_value_line(line)) {
      if (item->first == "MemTotal") {
        summary.total_bytes = parse_kib_value(item->second);
      } else if (item->first == "MemAvailable") {
        summary.available_bytes = parse_kib_value(item->second);
      } else if (item->first == "SwapTotal") {
        summary.swap_total_bytes = parse_kib_value(item->second);
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  return summary;
}

std::string truncate_hardware_input(std::string content) {
  if (content.size() <= kMaxHardwareInputBytes) {
    return content;
  }

  content.resize(kMaxHardwareInputBytes);
  const auto last_newline = content.find_last_of('\n');
  if (last_newline != std::string::npos && last_newline + 1 < content.size()) {
    content.resize(last_newline + 1);
  }
  return content;
}

std::string format_hardware_summary_body(const HardwareSummary& summary) {
  std::string body;
  bool truncated = false;

  append_bounded(body, "hardware_summary=1\n", truncated);
  append_bounded(body,
                 "architecture=" +
                     core::redact_likely_secret(unknown_if_empty(summary.architecture)) + '\n',
                 truncated);
  append_bounded(body,
                 "cpu_vendor=" + core::redact_likely_secret(unknown_if_empty(summary.cpu.vendor_id)) +
                     '\n',
                 truncated);
  append_bounded(body,
                 "cpu_model=" +
                     core::redact_likely_secret(unknown_if_empty(summary.cpu.model_name)) + '\n',
                 truncated);
  append_bounded(body,
                 "cpu_logical_processors=" +
                     std::to_string(summary.cpu.logical_processors) + '\n',
                 truncated);
  append_bounded(body,
                 "cpu_physical_cores=" + std::to_string(summary.cpu.physical_cores) + '\n',
                 truncated);
  append_bounded(body,
                 "memory_total_bytes=" + std::to_string(summary.memory.total_bytes) + '\n',
                 truncated);
  append_bounded(body,
                 "memory_available_bytes=" +
                     std::to_string(summary.memory.available_bytes) + '\n',
                 truncated);
  append_bounded(body,
                 "swap_total_bytes=" + std::to_string(summary.memory.swap_total_bytes) + '\n',
                 truncated);
  append_bounded(body,
                 "system_vendor=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.system_vendor)) + '\n',
                 truncated);
  append_bounded(body,
                 "product_name=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.product_name)) + '\n',
                 truncated);
  append_bounded(body,
                 "product_version=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.product_version)) +
                     '\n',
                 truncated);
  append_bounded(body,
                 "board_vendor=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.board_vendor)) + '\n',
                 truncated);
  append_bounded(body,
                 "board_name=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.board_name)) + '\n',
                 truncated);
  append_bounded(body,
                 "bios_vendor=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.bios_vendor)) + '\n',
                 truncated);
  append_bounded(body,
                 "bios_version=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.bios_version)) + '\n',
                 truncated);
  append_bounded(body,
                 "chassis_type=" +
                     core::redact_likely_secret(unknown_if_empty(summary.dmi.chassis_type)) + '\n',
                 truncated);
  append_bounded(body,
                 "gpu_devices_count=" + std::to_string(summary.gpu_devices.size()) + '\n',
                 truncated);

  for (const auto& row : summary.gpu_devices) {
    append_bounded(body, format_gpu_row(row), truncated);
    if (truncated) {
      return body;
    }
  }

  if (summary.has_more_gpu_devices) {
    append_bounded(body, "truncated=true\n", truncated);
  }
  return body;
}

}  // namespace detail

HardwareSummaryProvider default_hardware_summary_provider() {
  return [] {
    utsname uname_info{};
    if (uname(&uname_info) != 0) {
      return detail::HardwareSummaryResult{
          .ok = false,
          .error = "hardware summary is not available",
      };
    }

    detail::HardwareSummary summary{
        .architecture = uname_info.machine,
        .cpu = detail::parse_cpuinfo_summary(read_file_contents("/proc/cpuinfo")),
        .memory = detail::parse_meminfo_summary(read_file_contents("/proc/meminfo")),
        .dmi = detail::DmiSummary{
            .system_vendor = read_text_file("/sys/class/dmi/id/sys_vendor"),
            .product_name = read_text_file("/sys/class/dmi/id/product_name"),
            .product_version = read_text_file("/sys/class/dmi/id/product_version"),
            .board_vendor = read_text_file("/sys/class/dmi/id/board_vendor"),
            .board_name = read_text_file("/sys/class/dmi/id/board_name"),
            .bios_vendor = read_text_file("/sys/class/dmi/id/bios_vendor"),
            .bios_version = read_text_file("/sys/class/dmi/id/bios_version"),
            .chassis_type = read_text_file("/sys/class/dmi/id/chassis_type"),
        },
    };

    const std::filesystem::path drm_root = "/sys/class/drm";
    if (std::filesystem::exists(drm_root)) {
      std::vector<std::string> card_names;
      std::error_code error;
      for (const auto& entry : std::filesystem::directory_iterator(drm_root, error)) {
        const auto name = entry.path().filename().string();
        if (is_drm_card_name(name)) {
          card_names.push_back(name);
        }
      }
      std::ranges::sort(card_names);

      for (const auto& name : card_names) {
        const auto device_path = drm_root / name / "device";
        if (!std::filesystem::exists(device_path)) {
          continue;
        }

        if (summary.gpu_devices.size() < kMaxGpuRows) {
          summary.gpu_devices.push_back(detail::GpuDeviceRow{
              .name = name,
              .vendor_id = read_text_file(device_path / "vendor"),
              .device_id = read_text_file(device_path / "device"),
              .class_id = read_text_file(device_path / "class"),
              .driver = read_driver_name(device_path),
              .source = "sysfs",
          });
        } else {
          summary.has_more_gpu_devices = true;
        }
      }
    }

    return detail::HardwareSummaryResult{
        .ok = true,
        .error = "",
        .summary = std::move(summary),
    };
  };
}

HardwareSummaryTool::HardwareSummaryTool(HardwareSummaryProvider provider)
    : provider_(std::move(provider)) {}

std::string HardwareSummaryTool::name() const {
  return "hardware.summary";
}

core::RiskClass HardwareSummaryTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse HardwareSummaryTool::call(const core::ToolRequest& request) const {
  auto result = provider_();
  if (!result.ok) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = result.error.empty() ? "hardware summary provider failed" : result.error,
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "hardware summary collected",
      .evidence = {core::Evidence{
          .id = request.id + ":hardware.summary",
          .source = "hardware.summary",
          .summary = "bounded local hardware summary",
          .body = detail::format_hardware_summary_body(result.summary),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
