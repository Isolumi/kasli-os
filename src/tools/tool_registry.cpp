#include <kasli/tools/tool_registry.hpp>

#include <algorithm>
#include <stdexcept>

namespace kasli::tools {

void ToolRegistry::add(std::unique_ptr<Tool> tool) {
  if (tool == nullptr) {
    throw std::invalid_argument("tool registry cannot add null tool");
  }

  if (find(tool->name()) != nullptr) {
    throw std::invalid_argument("tool registry cannot add duplicate tool name");
  }

  tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(const std::string& name) const {
  const auto match = std::ranges::find_if(tools_, [&](const auto& tool) {
    return tool->name() == name;
  });
  if (match == tools_.end()) {
    return nullptr;
  }
  return match->get();
}

std::vector<std::string> ToolRegistry::names() const {
  std::vector<std::string> result;
  result.reserve(tools_.size());
  for (const auto& tool : tools_) {
    result.push_back(tool->name());
  }
  return result;
}

std::vector<policy::ToolPolicy> ToolRegistry::policies() const {
  std::vector<policy::ToolPolicy> result;
  result.reserve(tools_.size());
  for (const auto& tool : tools_) {
    result.push_back(policy::ToolPolicy{.tool_name = tool->name(), .risk = tool->risk()});
  }
  return result;
}

}  // namespace kasli::tools
