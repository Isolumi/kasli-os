#pragma once

#include <kasli/tools/tool.hpp>

#include <string>
#include <vector>

namespace kasli::tools {

namespace detail {

struct SystemdUnitListRow {
  std::string unit;
  std::string load_state;
  std::string active_state;
  std::string sub_state;
  std::string description;
};

struct SystemdUnitStatusEvidence {
  std::string unit;
  std::string id;
  std::string description;
  std::string load_state;
  std::string active_state;
  std::string sub_state;
  std::string unit_file_state;
  std::string fragment_path;
  bool is_service = false;
  std::string service_result;
  std::string exec_main_code;
  std::string exec_main_status;
};

std::string format_systemd_units_list_body(const std::vector<SystemdUnitListRow>& rows,
                                           bool has_more_rows);
std::string format_systemd_unit_status_body(const SystemdUnitStatusEvidence& evidence);

}  // namespace detail

class SystemdUnitsTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

class SystemdUnitStatusTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

}  // namespace kasli::tools
