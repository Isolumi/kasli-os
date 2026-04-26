#include <catch2/catch_test_macros.hpp>
#include <kasli/policy/policy_broker.hpp>

using kasli::core::RiskClass;
using kasli::core::ToolRequest;
using kasli::policy::ToolPolicy;

TEST_CASE("policy allows registered read-only tool") {
  kasli::policy::PolicyBroker broker({
      ToolPolicy{"system.info", RiskClass::ReadOnly},
      ToolPolicy{"journal.query", RiskClass::ReadOnly},
  });

  auto decision = broker.decide(ToolRequest{
      .id = "req-1",
      .tool_name = "system.info",
      .risk = RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE(decision.allowed);
  REQUIRE(decision.reason == "read-only tool allowed");
}

TEST_CASE("policy denies unregistered tool") {
  kasli::policy::PolicyBroker broker({ToolPolicy{"system.info", RiskClass::ReadOnly}});

  auto decision = broker.decide(ToolRequest{
      .id = "req-2",
      .tool_name = "shell.exec",
      .risk = RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE_FALSE(decision.allowed);
  REQUIRE(decision.reason == "tool is not registered in policy allowlist");
}

TEST_CASE("policy denies mutating risk classes in v1") {
  kasli::policy::PolicyBroker broker({ToolPolicy{"packages.install", RiskClass::Admin}});

  auto decision = broker.decide(ToolRequest{
      .id = "req-3",
      .tool_name = "packages.install",
      .risk = RiskClass::Admin,
      .params = {},
  });

  REQUIRE_FALSE(decision.allowed);
  REQUIRE(decision.reason == "only read-only tools are allowed in v1");
}

TEST_CASE("policy denies spoofed read-only risk for trusted mutating tool") {
  kasli::policy::PolicyBroker broker({ToolPolicy{"packages.install", RiskClass::Admin}});

  auto decision = broker.decide(ToolRequest{
      .id = "req-4",
      .tool_name = "packages.install",
      .risk = RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE_FALSE(decision.allowed);
  REQUIRE(decision.reason == "only read-only tools are allowed in v1");
}
