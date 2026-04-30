#include <kasli/tools/power_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxPowerRows = 16;
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

std::string format_power_row(const detail::PowerSupplyRow& row) {
  return "power_supply=name=" + core::redact_likely_secret(unknown_if_empty(row.name)) +
         " type=" + core::redact_likely_secret(unknown_if_empty(row.type)) +
         " status=" + core::redact_likely_secret(unknown_if_empty(row.status)) +
         " online=" + core::redact_likely_secret(unknown_if_empty(row.online)) +
         " capacity_percent=" +
         core::redact_likely_secret(unknown_if_empty(row.capacity_percent)) +
         " health=" + core::redact_likely_secret(unknown_if_empty(row.health)) +
         " technology=" + core::redact_likely_secret(unknown_if_empty(row.technology)) +
         " energy_now=" + core::redact_likely_secret(unknown_if_empty(row.energy_now)) +
         " energy_full=" + core::redact_likely_secret(unknown_if_empty(row.energy_full)) +
         " charge_now=" + core::redact_likely_secret(unknown_if_empty(row.charge_now)) +
         " charge_full=" + core::redact_likely_secret(unknown_if_empty(row.charge_full)) +
         " power_now=" + core::redact_likely_secret(unknown_if_empty(row.power_now)) +
         " voltage_now=" + core::redact_likely_secret(unknown_if_empty(row.voltage_now)) +
         " current_now=" + core::redact_likely_secret(unknown_if_empty(row.current_now)) +
         " cycle_count=" + core::redact_likely_secret(unknown_if_empty(row.cycle_count)) +
         " manufacturer=" + core::redact_likely_secret(unknown_if_empty(row.manufacturer)) +
         " model_name=" + core::redact_likely_secret(unknown_if_empty(row.model_name)) +
         " source=" + core::redact_likely_secret(unknown_if_empty(row.source)) + '\n';
}

}  // namespace

namespace detail {

std::string format_power_status_body(const std::vector<PowerSupplyRow>& rows,
                                     bool has_more_rows) {
  std::string body;
  bool truncated = false;

  append_bounded(body, "power_supplies_count=" + std::to_string(rows.size()) + '\n',
                 truncated);
  if (rows.empty()) {
    append_bounded(body, "no_power_supplies=true\n", truncated);
  }

  for (const auto& row : rows) {
    append_bounded(body, format_power_row(row), truncated);
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

PowerStatusProvider default_power_status_provider() {
  return [] {
    const std::filesystem::path power_root = "/sys/class/power_supply";
    if (!std::filesystem::exists(power_root)) {
      return detail::PowerStatusResult{
          .ok = false,
          .error = "power supply status is not available",
      };
    }

    std::vector<std::string> supply_names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(power_root, error)) {
      supply_names.push_back(entry.path().filename().string());
    }
    if (error) {
      return detail::PowerStatusResult{
          .ok = false,
          .error = "power supply status is not available",
      };
    }
    std::ranges::sort(supply_names);

    std::vector<detail::PowerSupplyRow> rows;
    bool has_more_rows = false;
    for (const auto& name : supply_names) {
      const auto supply_path = power_root / name;
      if (rows.size() < kMaxPowerRows) {
        rows.push_back(detail::PowerSupplyRow{
            .name = name,
            .type = read_text_file(supply_path / "type"),
            .status = read_text_file(supply_path / "status"),
            .online = read_text_file(supply_path / "online"),
            .capacity_percent = read_text_file(supply_path / "capacity"),
            .health = read_text_file(supply_path / "health"),
            .technology = read_text_file(supply_path / "technology"),
            .energy_now = read_text_file(supply_path / "energy_now"),
            .energy_full = read_text_file(supply_path / "energy_full"),
            .charge_now = read_text_file(supply_path / "charge_now"),
            .charge_full = read_text_file(supply_path / "charge_full"),
            .power_now = read_text_file(supply_path / "power_now"),
            .voltage_now = read_text_file(supply_path / "voltage_now"),
            .current_now = read_text_file(supply_path / "current_now"),
            .cycle_count = read_text_file(supply_path / "cycle_count"),
            .manufacturer = read_text_file(supply_path / "manufacturer"),
            .model_name = read_text_file(supply_path / "model_name"),
            .source = "sysfs",
        });
      } else {
        has_more_rows = true;
      }
    }

    return detail::PowerStatusResult{
        .ok = true,
        .error = "",
        .rows = std::move(rows),
        .has_more_rows = has_more_rows,
    };
  };
}

PowerStatusTool::PowerStatusTool(PowerStatusProvider provider) : provider_(std::move(provider)) {}

std::string PowerStatusTool::name() const {
  return "power.status";
}

core::RiskClass PowerStatusTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse PowerStatusTool::call(const core::ToolRequest& request) const {
  auto result = provider_();
  if (!result.ok) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = result.error.empty() ? "power status provider failed" : result.error,
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "power status listed",
      .evidence = {core::Evidence{
          .id = request.id + ":power.status",
          .source = "power.status",
          .summary = "bounded local power supply status",
          .body = detail::format_power_status_body(result.rows, result.has_more_rows),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
