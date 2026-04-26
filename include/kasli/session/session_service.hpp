#pragma once

#include <kasli/audit/audit_log.hpp>
#include <kasli/model/model_provider.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <string>
#include <vector>

namespace kasli::session {

struct AskResult {
  bool ok = false;
  std::string answer;
  std::string error;
};

class SessionService {
 public:
  SessionService(const tools::ToolRegistry& registry,
                 const policy::PolicyBroker& policy,
                 const audit::AuditLog& audit);

  std::vector<std::string> list_tools() const;
  core::ToolResponse call_tool(const core::ToolRequest& request, const std::string& actor) const;
  AskResult ask_with_tool(const std::string& prompt,
                          const core::ToolRequest& request,
                          const model::ModelProvider& model,
                          const std::string& actor) const;

 private:
  const tools::ToolRegistry& registry_;
  const policy::PolicyBroker& policy_;
  const audit::AuditLog& audit_;
};

}  // namespace kasli::session
