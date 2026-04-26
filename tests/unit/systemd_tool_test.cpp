#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/systemd_tool.hpp>

TEST_CASE("systemd units tool reports unavailable when systemd is not built") {
#if !KASLI_HAS_SYSTEMD
  kasli::tools::SystemdUnitsTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-unavailable",
      .tool_name = "systemd.units.list",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd support was not built");
  REQUIRE(response.evidence.empty());
#endif
}

TEST_CASE("systemd unit status tool metadata is read-only") {
  kasli::tools::SystemdUnitStatusTool tool;

  REQUIRE(tool.name() == "systemd.unit.status");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("systemd unit status tool reports unavailable when systemd is not built") {
#if !KASLI_HAS_SYSTEMD
  kasli::tools::SystemdUnitStatusTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-status-unavailable",
      .tool_name = "systemd.unit.status",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd support was not built");
  REQUIRE(response.evidence.empty());
#endif
}

TEST_CASE("systemd unit status tool requires unit when systemd is built") {
#if KASLI_HAS_SYSTEMD
  kasli::tools::SystemdUnitStatusTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-status-missing-unit",
      .tool_name = "systemd.unit.status",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd.unit.status requires a non-empty unit parameter");
  REQUIRE(response.evidence.empty());
#endif
}
