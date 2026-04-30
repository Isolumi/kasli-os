#pragma once

#include <kasli/tools/tool.hpp>

#include <functional>
#include <string>
#include <vector>

namespace kasli::tools {

namespace detail {

struct PowerSupplyRow {
  std::string name;
  std::string type;
  std::string status;
  std::string online;
  std::string capacity_percent;
  std::string health;
  std::string technology;
  std::string energy_now;
  std::string energy_full;
  std::string charge_now;
  std::string charge_full;
  std::string power_now;
  std::string voltage_now;
  std::string current_now;
  std::string cycle_count;
  std::string manufacturer;
  std::string model_name;
  std::string source;
};

struct PowerStatusResult {
  bool ok = false;
  std::string error;
  std::vector<PowerSupplyRow> rows;
  bool has_more_rows = false;
};

std::string format_power_status_body(const std::vector<PowerSupplyRow>& rows,
                                     bool has_more_rows);

}  // namespace detail

using PowerStatusProvider = std::function<detail::PowerStatusResult()>;

PowerStatusProvider default_power_status_provider();

class PowerStatusTool final : public Tool {
 public:
  explicit PowerStatusTool(PowerStatusProvider provider = default_power_status_provider());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  PowerStatusProvider provider_;
};

}  // namespace kasli::tools
