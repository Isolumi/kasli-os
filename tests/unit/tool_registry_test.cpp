#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <memory>
#include <stdexcept>

TEST_CASE("tool registry lists registered tools") {
  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  auto names = registry.names();
  REQUIRE(names.size() == 1);
  REQUIRE(names.at(0) == "system.info");
  REQUIRE(registry.find("system.info") != nullptr);
  REQUIRE(registry.find("shell.exec") == nullptr);
}

TEST_CASE("tool registry exposes trusted policy metadata") {
  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  auto policies = registry.policies();
  REQUIRE(policies.size() == 1);
  REQUIRE(policies.at(0).tool_name == "system.info");
  REQUIRE(policies.at(0).risk == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("tool registry rejects duplicate tool names") {
  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  REQUIRE_THROWS_AS(
      registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release")),
      std::invalid_argument);
}

TEST_CASE("tool registry rejects null tools") {
  kasli::tools::ToolRegistry registry;

  REQUIRE_THROWS_AS(registry.add(nullptr), std::invalid_argument);
}
