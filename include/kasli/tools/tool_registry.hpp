#pragma once

#include <kasli/policy/policy_broker.hpp>
#include <kasli/tools/tool.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kasli::tools {

class ToolRegistry {
 public:
  void add(std::unique_ptr<Tool> tool);
  const Tool* find(const std::string& name) const;
  std::vector<std::string> names() const;
  std::vector<policy::ToolPolicy> policies() const;

 private:
  std::vector<std::unique_ptr<Tool>> tools_;
};

}  // namespace kasli::tools
