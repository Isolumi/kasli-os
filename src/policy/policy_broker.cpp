#include <kasli/policy/policy_broker.hpp>

namespace kasli::policy {

PolicyBroker::PolicyBroker(std::vector<ToolPolicy> allowed_tools) {
  for (const auto& tool : allowed_tools) {
    allowed_tools_.emplace(tool.tool_name, tool.risk);
  }
}

PolicyDecision PolicyBroker::decide(const core::ToolRequest& request) const {
  const auto tool = allowed_tools_.find(request.tool_name);
  if (tool == allowed_tools_.end()) {
    return PolicyDecision{.allowed = false, .reason = "tool is not registered in policy allowlist"};
  }
  if (tool->second != core::RiskClass::ReadOnly) {
    return PolicyDecision{.allowed = false, .reason = "only read-only tools are allowed in v1"};
  }
  return PolicyDecision{.allowed = true, .reason = "read-only tool allowed"};
}

}  // namespace kasli::policy
