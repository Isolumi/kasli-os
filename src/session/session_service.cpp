#include <kasli/session/session_service.hpp>

#include <kasli/core/uuid.hpp>

#include <exception>

namespace kasli::session {

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
      .timestamp = "",
      .actor = actor,
      .type = "tool.call",
      .summary = request.tool_name,
      .details = {{"request_id", request.id},
                  {"policy_decision", decision.reason},
                  {"status", core::to_string(response.status)}},
  });

  return response;
}

}  // namespace kasli::session
