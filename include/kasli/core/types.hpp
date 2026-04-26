#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kasli::core {

enum class RiskClass {
  ReadOnly,
  LowRiskUser,
  Admin,
  Destructive,
};

enum class ToolStatus {
  Ok,
  Denied,
  Error,
};

struct Evidence {
  std::string id;
  std::string source;
  std::string summary;
  std::string body;
  std::string timestamp;
};

struct ToolRequest {
  std::string id;
  std::string tool_name;
  RiskClass risk = RiskClass::ReadOnly;
  std::map<std::string, std::string> params;
};

struct ToolResponse {
  std::string request_id;
  ToolStatus status = ToolStatus::Error;
  std::string message;
  std::vector<Evidence> evidence;
};

struct AuditEvent {
  std::string id;
  std::string timestamp;
  std::string actor;
  std::string type;
  std::string summary;
  std::map<std::string, std::string> details;
};

std::string to_string(RiskClass risk);
RiskClass risk_class_from_string(const std::string& value);
std::string to_string(ToolStatus status);
ToolStatus tool_status_from_string(const std::string& value);

void to_json(nlohmann::json& json, const Evidence& evidence);
void from_json(const nlohmann::json& json, Evidence& evidence);
void to_json(nlohmann::json& json, const ToolRequest& request);
void from_json(const nlohmann::json& json, ToolRequest& request);
void to_json(nlohmann::json& json, const ToolResponse& response);
void from_json(const nlohmann::json& json, ToolResponse& response);
void to_json(nlohmann::json& json, const AuditEvent& event);
void from_json(const nlohmann::json& json, AuditEvent& event);

}  // namespace kasli::core
