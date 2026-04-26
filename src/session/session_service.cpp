#include <kasli/session/session_service.hpp>

#include <kasli/core/json.hpp>
#include <kasli/core/uuid.hpp>

#include <cstddef>
#include <exception>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace kasli::session {
namespace {

constexpr std::size_t kMaxAuditStringBytes = 2048;
constexpr std::size_t kMaxAuditJsonBytes = 4096;
constexpr std::size_t kMaxAuditParamValueBytes = 512;
constexpr std::size_t kMaxAuditEvidenceRefs = 64;

std::string bound_string(const std::string& value, std::size_t max_bytes) {
  const std::string redacted = core::redact_likely_secret(value);
  if (redacted.size() <= max_bytes) {
    return redacted;
  }

  constexpr std::string_view marker = "...[truncated]";
  if (max_bytes <= marker.size()) {
    return std::string(marker.substr(0, max_bytes));
  }
  return redacted.substr(0, max_bytes - marker.size()) + std::string(marker);
}

std::string bound_audit_string(const std::string& value) {
  return bound_string(value, kMaxAuditStringBytes);
}

nlohmann::json bounded_params_json(const std::map<std::string, std::string>& params) {
  nlohmann::json output = nlohmann::json::object();
  for (const auto& [key, value] : params) {
    output[key] = bound_string(value, kMaxAuditParamValueBytes);
  }
  return output;
}

nlohmann::json evidence_refs_json(const std::vector<core::Evidence>& evidence) {
  nlohmann::json refs = nlohmann::json::array();
  std::size_t count = 0;
  for (const auto& item : evidence) {
    if (count >= kMaxAuditEvidenceRefs) {
      refs.push_back(nlohmann::json{{"truncated", true}});
      break;
    }
    refs.push_back(nlohmann::json{
        {"id", bound_audit_string(item.id)},
        {"source", bound_audit_string(item.source)},
        {"summary", bound_audit_string(item.summary)},
        {"timestamp", bound_audit_string(item.timestamp)},
    });
    ++count;
  }
  return refs;
}

std::string bound_json(const nlohmann::json& value) {
  return bound_string(value.dump(), kMaxAuditJsonBytes);
}

std::map<std::string, std::string> tool_audit_details(
    const core::ToolRequest& request,
    const policy::PolicyDecision& decision,
    const core::ToolResponse& response) {
  return {{"request_id", bound_audit_string(request.id)},
          {"tool_name", bound_audit_string(request.tool_name)},
          {"request_risk", core::to_string(request.risk)},
          {"request_params", bound_json(bounded_params_json(request.params))},
          {"policy_allowed", decision.allowed ? "true" : "false"},
          {"policy_decision", bound_audit_string(decision.reason)},
          {"response_status", core::to_string(response.status)},
          {"response_message", bound_audit_string(response.message)},
          {"response_evidence_count", std::to_string(response.evidence.size())},
          {"response_evidence_refs", bound_json(evidence_refs_json(response.evidence))},
          {"status", core::to_string(response.status)}};
}

std::map<std::string, std::string> model_context_details(
    const std::string& prompt,
    const core::ToolRequest& request,
    const std::vector<core::Evidence>& evidence) {
  return {{"request_id", bound_audit_string(request.id)},
          {"prompt", bound_audit_string(prompt)},
          {"evidence_count", std::to_string(evidence.size())},
          {"evidence_refs", bound_json(evidence_refs_json(evidence))}};
}

}  // namespace

SessionService::SessionService(const tools::ToolRegistry& registry,
                               const policy::PolicyBroker& policy,
                               const audit::AuditLog& audit)
    : registry_(registry), policy_(policy), audit_(audit) {}

std::vector<std::string> SessionService::list_tools() const {
  return registry_.names();
}

core::ToolResponse SessionService::call_tool(const core::ToolRequest& request,
                                             const std::string& actor) const {
  const auto decision = policy_.decide(request);
  core::ToolResponse response;
  if (!decision.allowed) {
    response = core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Denied,
        .message = decision.reason,
        .evidence = {},
    };
  } else if (const tools::Tool* tool = registry_.find(request.tool_name); tool == nullptr) {
    response = core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Denied,
        .message = "tool is not registered",
        .evidence = {},
    };
  } else {
    try {
      response = tool->call(request);
    } catch (const std::exception& error) {
      response = core::ToolResponse{
          .request_id = request.id,
          .status = core::ToolStatus::Error,
          .message = error.what(),
          .evidence = {},
      };
    } catch (...) {
      response = core::ToolResponse{
          .request_id = request.id,
          .status = core::ToolStatus::Error,
          .message = "tool call failed with unknown exception",
          .evidence = {},
      };
    }
  }

  audit_.append(core::AuditEvent{
      .id = core::make_event_id(),
      .timestamp = core::utc_timestamp(),
      .actor = actor,
      .type = "tool.call",
      .summary = bound_audit_string(request.tool_name),
      .details = tool_audit_details(request, decision, response),
  });

  return response;
}

AskResult SessionService::ask_with_tool(const std::string& prompt,
                                        const core::ToolRequest& request,
                                        const model::ModelProvider& model,
                                        const std::string& actor) const {
  auto tool_response = call_tool(request, actor);
  if (tool_response.status != core::ToolStatus::Ok) {
    audit_.append(core::AuditEvent{
        .id = core::make_event_id(),
        .timestamp = core::utc_timestamp(),
        .actor = actor,
        .type = "model.skipped",
        .summary = bound_audit_string(prompt),
        .details = {{"request_id", bound_audit_string(request.id)},
                    {"prompt", bound_audit_string(prompt)},
                    {"tool_status", core::to_string(tool_response.status)},
                    {"reason", bound_audit_string(tool_response.message)},
                    {"evidence_count", std::to_string(tool_response.evidence.size())},
                    {"evidence_refs", bound_json(evidence_refs_json(tool_response.evidence))}},
    });
    return AskResult{
        .ok = false,
        .answer = "",
        .error = "tool call did not succeed: " + tool_response.message,
    };
  }

  audit_.append(core::AuditEvent{
      .id = core::make_event_id(),
      .timestamp = core::utc_timestamp(),
      .actor = actor,
      .type = "model.request",
      .summary = bound_audit_string(prompt),
      .details = model_context_details(prompt, request, tool_response.evidence),
  });

  try {
    const auto answer =
        model.complete(model::ModelRequest{.prompt = prompt, .evidence = tool_response.evidence});
    audit_.append(core::AuditEvent{
        .id = core::make_event_id(),
        .timestamp = core::utc_timestamp(),
        .actor = actor,
        .type = "model.response",
        .summary = "model response received",
        .details = {{"request_id", bound_audit_string(request.id)},
                    {"response_status", "ok"},
                    {"response_message", "model response received"},
                    {"response_bytes", std::to_string(answer.size())},
                    {"response_preview", bound_audit_string(answer)}},
    });
    return AskResult{
        .ok = true,
        .answer = answer,
        .error = "",
    };
  } catch (const std::exception& error) {
    audit_.append(core::AuditEvent{
        .id = core::make_event_id(),
        .timestamp = core::utc_timestamp(),
        .actor = actor,
        .type = "model.error",
        .summary = bound_audit_string(prompt),
        .details = {{"request_id", bound_audit_string(request.id)},
                    {"prompt", bound_audit_string(prompt)},
                    {"error", bound_audit_string(error.what())},
                    {"evidence_count", std::to_string(tool_response.evidence.size())},
                    {"evidence_refs", bound_json(evidence_refs_json(tool_response.evidence))}},
    });
    return AskResult{.ok = false, .answer = "", .error = error.what()};
  } catch (...) {
    const std::string message = "model failed with unknown exception";
    audit_.append(core::AuditEvent{
        .id = core::make_event_id(),
        .timestamp = core::utc_timestamp(),
        .actor = actor,
        .type = "model.error",
        .summary = bound_audit_string(prompt),
        .details = {{"request_id", bound_audit_string(request.id)},
                    {"prompt", bound_audit_string(prompt)},
                    {"error", message},
                    {"evidence_count", std::to_string(tool_response.evidence.size())},
                    {"evidence_refs", bound_json(evidence_refs_json(tool_response.evidence))}},
    });
    return AskResult{.ok = false, .answer = "", .error = message};
  }
}

}  // namespace kasli::session
