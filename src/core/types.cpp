#include <kasli/core/types.hpp>

#include <stdexcept>

namespace kasli::core {

std::string to_string(RiskClass risk) {
  switch (risk) {
    case RiskClass::ReadOnly:
      return "read_only";
    case RiskClass::LowRiskUser:
      return "low_risk_user";
    case RiskClass::Admin:
      return "admin";
    case RiskClass::Destructive:
      return "destructive";
  }
  throw std::invalid_argument("unknown risk class");
}

RiskClass risk_class_from_string(const std::string& value) {
  if (value == "read_only") return RiskClass::ReadOnly;
  if (value == "low_risk_user") return RiskClass::LowRiskUser;
  if (value == "admin") return RiskClass::Admin;
  if (value == "destructive") return RiskClass::Destructive;
  throw std::invalid_argument("unknown risk class: " + value);
}

std::string to_string(ToolStatus status) {
  switch (status) {
    case ToolStatus::Ok:
      return "ok";
    case ToolStatus::Denied:
      return "denied";
    case ToolStatus::Error:
      return "error";
  }
  throw std::invalid_argument("unknown tool status");
}

ToolStatus tool_status_from_string(const std::string& value) {
  if (value == "ok") return ToolStatus::Ok;
  if (value == "denied") return ToolStatus::Denied;
  if (value == "error") return ToolStatus::Error;
  throw std::invalid_argument("unknown tool status: " + value);
}

void to_json(nlohmann::json& json, const Evidence& evidence) {
  json = nlohmann::json{
      {"id", evidence.id},
      {"source", evidence.source},
      {"summary", evidence.summary},
      {"body", evidence.body},
      {"timestamp", evidence.timestamp},
  };
}

void from_json(const nlohmann::json& json, Evidence& evidence) {
  evidence.id = json.at("id").get<std::string>();
  evidence.source = json.at("source").get<std::string>();
  evidence.summary = json.at("summary").get<std::string>();
  evidence.body = json.at("body").get<std::string>();
  evidence.timestamp = json.at("timestamp").get<std::string>();
}

void to_json(nlohmann::json& json, const ToolRequest& request) {
  json = nlohmann::json{
      {"id", request.id},
      {"tool_name", request.tool_name},
      {"risk", to_string(request.risk)},
      {"params", request.params},
  };
}

void from_json(const nlohmann::json& json, ToolRequest& request) {
  request.id = json.at("id").get<std::string>();
  request.tool_name = json.at("tool_name").get<std::string>();
  request.risk = risk_class_from_string(json.at("risk").get<std::string>());
  request.params = json.value("params", std::map<std::string, std::string>{});
}

void to_json(nlohmann::json& json, const ToolResponse& response) {
  json = nlohmann::json{
      {"request_id", response.request_id},
      {"status", to_string(response.status)},
      {"message", response.message},
      {"evidence", response.evidence},
  };
}

void from_json(const nlohmann::json& json, ToolResponse& response) {
  response.request_id = json.at("request_id").get<std::string>();
  response.status = tool_status_from_string(json.at("status").get<std::string>());
  response.message = json.at("message").get<std::string>();
  response.evidence = json.value("evidence", std::vector<Evidence>{});
}

void to_json(nlohmann::json& json, const AuditEvent& event) {
  json = nlohmann::json{
      {"id", event.id},
      {"timestamp", event.timestamp},
      {"actor", event.actor},
      {"type", event.type},
      {"summary", event.summary},
      {"details", event.details},
  };
}

void from_json(const nlohmann::json& json, AuditEvent& event) {
  event.id = json.at("id").get<std::string>();
  event.timestamp = json.at("timestamp").get<std::string>();
  event.actor = json.at("actor").get<std::string>();
  event.type = json.at("type").get<std::string>();
  event.summary = json.at("summary").get<std::string>();
  event.details = json.value("details", std::map<std::string, std::string>{});
}

}  // namespace kasli::core
