#pragma once

#include <kasli/core/types.hpp>

#include <map>
#include <string>
#include <vector>

namespace kasli::policy {

struct PolicyDecision {
  bool allowed = false;
  std::string reason;
};

struct ToolPolicy {
  std::string tool_name;
  core::RiskClass risk = core::RiskClass::ReadOnly;
};

class PolicyBroker {
 public:
  explicit PolicyBroker(std::vector<ToolPolicy> allowed_tools);

  PolicyDecision decide(const core::ToolRequest& request) const;

 private:
  std::map<std::string, core::RiskClass> allowed_tools_;
};

}  // namespace kasli::policy
