#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/system_info_tool.hpp>

TEST_CASE("system info tool returns os-release evidence") {
  kasli::tools::SystemInfoTool tool("tests/fixtures/os-release");
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.request_id == "req-1");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).source == "system.info");
  REQUIRE(response.evidence.at(0).body.find("Kasli Test Linux") != std::string::npos);
}
