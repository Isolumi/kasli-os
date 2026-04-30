#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/power_tool.hpp>

#include <string>
#include <vector>

TEST_CASE("power status tool metadata is read-only") {
  kasli::tools::PowerStatusTool tool([] {
    return kasli::tools::detail::PowerStatusResult{.ok = true};
  });

  REQUIRE(tool.name() == "power.status");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("power status body lists bounded battery and AC rows") {
  const auto body = kasli::tools::detail::format_power_status_body(
      std::vector<kasli::tools::detail::PowerSupplyRow>{
          kasli::tools::detail::PowerSupplyRow{
              .name = "AC",
              .type = "Mains",
              .status = "unknown",
              .online = "1",
              .capacity_percent = "unknown",
              .health = "unknown",
              .technology = "unknown",
              .energy_now = "unknown",
              .energy_full = "unknown",
              .charge_now = "unknown",
              .charge_full = "unknown",
              .power_now = "unknown",
              .voltage_now = "unknown",
              .current_now = "unknown",
              .cycle_count = "unknown",
              .manufacturer = "unknown",
              .model_name = "unknown",
              .source = "sysfs",
          },
          kasli::tools::detail::PowerSupplyRow{
              .name = "BAT0",
              .type = "Battery",
              .status = "Discharging",
              .online = "unknown",
              .capacity_percent = "87",
              .health = "Good",
              .technology = "Li-ion",
              .energy_now = "41000000",
              .energy_full = "50000000",
              .charge_now = "unknown",
              .charge_full = "unknown",
              .power_now = "12000000",
              .voltage_now = "11500000",
              .current_now = "unknown",
              .cycle_count = "42",
              .manufacturer = "Example",
              .model_name = "ExamplePack",
              .source = "sysfs",
          },
      },
      true);

  REQUIRE(body.find("power_supplies_count=2") != std::string::npos);
  REQUIRE(body.find("power_supply=name=AC type=Mains status=unknown online=1") !=
          std::string::npos);
  REQUIRE(body.find("power_supply=name=BAT0 type=Battery status=Discharging "
                    "online=unknown capacity_percent=87") != std::string::npos);
  REQUIRE(body.find("manufacturer=Example model_name=ExamplePack source=sysfs") !=
          std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("power status body reports empty hosts") {
  const auto body = kasli::tools::detail::format_power_status_body({}, false);

  REQUIRE(body.find("power_supplies_count=0") != std::string::npos);
  REQUIRE(body.find("no_power_supplies=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
}

TEST_CASE("power status tool reads injected provider rows") {
  kasli::tools::PowerStatusTool tool([] {
    return kasli::tools::detail::PowerStatusResult{
        .ok = true,
        .rows = {kasli::tools::detail::PowerSupplyRow{
            .name = "BAT0",
            .type = "Battery",
            .status = "Charging",
            .online = "unknown",
            .capacity_percent = "75",
            .source = "sysfs",
        }},
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-power-status",
      .tool_name = "power.status",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "power status listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "power.status");
  REQUIRE(response.evidence.front().body.find("power_supplies_count=1") != std::string::npos);
  REQUIRE(response.evidence.front().body.find("status=Charging") != std::string::npos);
}

TEST_CASE("power status tool reports provider errors") {
  kasli::tools::PowerStatusTool tool([] {
    return kasli::tools::detail::PowerStatusResult{
        .ok = false,
        .error = "power supply status is not available",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-power-status-error",
      .tool_name = "power.status",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "power supply status is not available");
  REQUIRE(response.evidence.empty());
}
