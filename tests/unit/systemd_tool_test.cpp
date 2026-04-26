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
