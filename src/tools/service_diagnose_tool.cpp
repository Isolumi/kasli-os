#include <kasli/tools/service_diagnose_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxDiagnosisBodyBytes = 12 * 1024;

bool is_blank(const std::string& value) {
  return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::string required_unit_param(const core::ToolRequest& request) {
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";
  if (is_blank(unit)) {
    return "";
  }
  return unit;
}

void append_bounded(std::string& body, const std::string& text, bool& truncated) {
  if (body.size() + text.size() <= kMaxDiagnosisBodyBytes) {
    body += text;
    return;
  }

  constexpr std::string_view marker = "truncated=true\n";
  const auto remaining = kMaxDiagnosisBodyBytes - body.size();
  if (remaining > marker.size()) {
    body += text.substr(0, remaining - marker.size());
  } else if (body.size() + marker.size() > kMaxDiagnosisBodyBytes) {
    body.resize(kMaxDiagnosisBodyBytes - marker.size());
  }
  body += marker;
  truncated = true;
}

std::map<std::string, std::string> parse_key_value_lines(const std::string& body) {
  std::map<std::string, std::string> values;
  std::istringstream input(body);
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos || separator == 0) {
      continue;
    }
    values.emplace(line.substr(0, separator), line.substr(separator + 1));
  }
  return values;
}

bool has_non_empty_evidence_body(const core::ToolResponse& response) {
  return std::ranges::any_of(response.evidence, [](const core::Evidence& evidence) {
    return !is_blank(evidence.body);
  });
}

bool contains_failure_terms(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });

  return text.find("failed") != std::string::npos || text.find("failure") != std::string::npos ||
         text.find("error") != std::string::npos || text.find("denied") != std::string::npos ||
         text.find("refused") != std::string::npos ||
         text.find("address already in use") != std::string::npos;
}

std::string first_evidence_body(const core::ToolResponse& response) {
  if (response.evidence.empty()) {
    return "";
  }
  return response.evidence.front().body;
}

void append_line(std::string& body,
                 const std::string& key,
                 const std::string& value,
                 bool& truncated) {
  append_bounded(body, key + '=' + core::redact_likely_secret(value) + '\n', truncated);
}

core::ToolRequest child_request(const core::ToolRequest& request,
                                const std::string& suffix,
                                const std::string& tool_name,
                                const std::string& unit) {
  return core::ToolRequest{
      .id = request.id + ':' + suffix,
      .tool_name = tool_name,
      .risk = core::RiskClass::ReadOnly,
      .params = {{"unit", unit}},
  };
}

core::ToolResponse missing_unit_response(const core::ToolRequest& request) {
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "service.diagnose requires a non-empty unit parameter",
      .evidence = {},
  };
}

}  // namespace

ServiceDiagnoseTool::ServiceDiagnoseTool(const Tool& status_tool, const Tool& journal_tool)
    : status_tool_(status_tool), journal_tool_(journal_tool) {}

std::string ServiceDiagnoseTool::name() const {
  return "service.diagnose";
}

core::RiskClass ServiceDiagnoseTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse ServiceDiagnoseTool::call(const core::ToolRequest& request) const {
  const auto unit = required_unit_param(request);
  if (unit.empty()) {
    return missing_unit_response(request);
  }

  const auto status_response =
      status_tool_.call(child_request(request, "status", "systemd.unit.status", unit));
  const auto journal_response =
      journal_tool_.call(child_request(request, "journal", "journal.query", unit));

  const bool status_ok = status_response.status == core::ToolStatus::Ok;
  const bool journal_ok = journal_response.status == core::ToolStatus::Ok;
  const bool any_ok = status_ok || journal_ok;

  const auto status_values = parse_key_value_lines(first_evidence_body(status_response));
  const std::string journal_body = first_evidence_body(journal_response);

  std::string body;
  bool truncated = false;
  append_line(body, "unit", unit, truncated);
  append_line(body, "status_tool_status", core::to_string(status_response.status), truncated);
  append_line(body, "status_tool_message", status_response.message, truncated);
  append_line(body, "journal_tool_status", core::to_string(journal_response.status), truncated);
  append_line(body, "journal_tool_message", journal_response.message, truncated);

  for (const auto& key : {"load_state",
                          "active_state",
                          "sub_state",
                          "unit_file_state",
                          "fragment_path",
                          "service_result",
                          "exec_main_code",
                          "exec_main_status"}) {
    const auto match = status_values.find(key);
    if (match != status_values.end() && !match->second.empty()) {
      append_line(body, key, match->second, truncated);
    }
  }

  append_line(body,
              "journal_entries_present",
              has_non_empty_evidence_body(journal_response) ? "true" : "false",
              truncated);
  append_line(body,
              "journal_failure_terms_present",
              contains_failure_terms(journal_body) ? "true" : "false",
              truncated);

  std::string diagnosis = "partial evidence collected";
  const auto active_state = status_values.find("active_state");
  const auto service_result = status_values.find("service_result");
  if (active_state != status_values.end() && active_state->second == "failed") {
    diagnosis = "service is failed";
  } else if (service_result != status_values.end() && !service_result->second.empty() &&
             service_result->second != "success") {
    diagnosis = "service reports a non-success result";
  } else if (active_state != status_values.end() && active_state->second == "active") {
    diagnosis = "service is active";
  } else if (!status_ok && journal_ok) {
    diagnosis = "status unavailable; journal evidence collected";
  } else if (status_ok && !journal_ok) {
    diagnosis = "status collected; journal unavailable";
  }
  append_line(body, "diagnosis", diagnosis, truncated);

  std::vector<core::Evidence> evidence;
  evidence.push_back(core::Evidence{
      .id = request.id + ":service.diagnose",
      .source = "service.diagnose",
      .summary = "aggregated service diagnosis for " + unit,
      .body = body,
      .timestamp = core::utc_timestamp(),
  });
  evidence.insert(evidence.end(), status_response.evidence.begin(), status_response.evidence.end());
  evidence.insert(evidence.end(), journal_response.evidence.begin(), journal_response.evidence.end());

  return core::ToolResponse{
      .request_id = request.id,
      .status = any_ok ? core::ToolStatus::Ok : core::ToolStatus::Error,
      .message = any_ok ? "service diagnosis collected" : "service diagnosis failed",
      .evidence = std::move(evidence),
  };
}

}  // namespace kasli::tools
