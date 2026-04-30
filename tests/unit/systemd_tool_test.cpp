#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/systemd_tool.hpp>

#include <string>
#include <vector>

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

TEST_CASE("failed services tool metadata is read-only") {
  kasli::tools::FailedServicesTool tool;

  REQUIRE(tool.name() == "services.failed");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("failed services body lists bounded failed service rows") {
  const auto body = kasli::tools::detail::format_failed_services_body(
      std::vector<kasli::tools::detail::FailedServiceRow>{
          {
              .unit = "sshd.service",
              .load_state = "loaded",
              .active_state = "failed",
              .sub_state = "failed",
              .description = "OpenSSH server daemon",
          },
          {
              .unit = "postgresql.service",
              .load_state = "loaded",
              .active_state = "failed",
              .sub_state = "failed",
              .description = "PostgreSQL database server",
          },
      },
      true);

  REQUIRE(body.find("failed_services_count=2") != std::string::npos);
  REQUIRE(body.find("sshd.service load=loaded active=failed sub=failed") != std::string::npos);
  REQUIRE(body.find("postgresql.service load=loaded active=failed sub=failed") !=
          std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
  REQUIRE(body.find("no_failed_services=true") == std::string::npos);
}

TEST_CASE("failed services body reports when no failed services are found") {
  const auto body = kasli::tools::detail::format_failed_services_body({}, false);

  REQUIRE(body.find("failed_services_count=0") != std::string::npos);
  REQUIRE(body.find("no_failed_services=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
}

TEST_CASE("failed services tool reports unavailable when systemd is not built") {
#if !KASLI_HAS_SYSTEMD
  kasli::tools::FailedServicesTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-services-failed-unavailable",
      .tool_name = "services.failed",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd support was not built");
  REQUIRE(response.evidence.empty());
#endif
}

TEST_CASE("failed services tool succeeds against a live system bus") {
#if KASLI_HAS_SYSTEMD
  kasli::tools::FailedServicesTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-services-failed-live",
      .tool_name = "services.failed",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  if (response.status == kasli::core::ToolStatus::Error &&
      response.message == "failed to connect to systemd system bus") {
    SKIP("systemd system bus is unavailable");
  }

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "failed services listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "services.failed");
#endif
}

TEST_CASE("systemd units list evidence is bounded and marks extra rows") {
  std::vector<kasli::tools::detail::SystemdUnitListRow> rows;
  rows.reserve(200);
  for (int index = 0; index < 200; ++index) {
    rows.push_back(kasli::tools::detail::SystemdUnitListRow{
        .unit = "unit-" + std::to_string(index) + ".service",
        .load_state = "loaded",
        .active_state = "active",
        .sub_state = "running",
        .description = "test service",
    });
  }

  const auto body = kasli::tools::detail::format_systemd_units_list_body(rows, true);

  REQUIRE(body.find("unit-0.service") != std::string::npos);
  REQUIRE(body.find("unit-199.service") != std::string::npos);
  REQUIRE(body.find("unit-200.service") == std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("systemd units list succeeds against a live system bus") {
#if KASLI_HAS_SYSTEMD
  kasli::tools::SystemdUnitsTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-live-list",
      .tool_name = "systemd.units.list",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  if (response.status == kasli::core::ToolStatus::Error &&
      response.message == "failed to connect to systemd system bus") {
    SKIP("systemd system bus is unavailable");
  }

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "systemd units listed");
  REQUIRE(response.evidence.size() == 1);
#endif
}

TEST_CASE("systemd unit status tool metadata is read-only") {
  kasli::tools::SystemdUnitStatusTool tool;

  REQUIRE(tool.name() == "systemd.unit.status");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("systemd unit status evidence includes failed service fields") {
  const auto body = kasli::tools::detail::format_systemd_unit_status_body(
      kasli::tools::detail::SystemdUnitStatusEvidence{
          .unit = "ssh.service",
          .id = "ssh.service",
          .description = "OpenSSH server",
          .load_state = "loaded",
          .active_state = "failed",
          .sub_state = "failed",
          .unit_file_state = "enabled",
          .fragment_path = "/usr/lib/systemd/system/ssh.service",
          .is_service = true,
          .service_result = "exit-code",
          .exec_main_code = "1",
          .exec_main_status = "255",
      });

  REQUIRE(body.find("unit=ssh.service") != std::string::npos);
  REQUIRE(body.find("active_state=failed") != std::string::npos);
  REQUIRE(body.find("service_result=exit-code") != std::string::npos);
  REQUIRE(body.find("exec_main_code=1") != std::string::npos);
  REQUIRE(body.find("exec_main_status=255") != std::string::npos);
}

TEST_CASE("systemd unit status evidence omits service fields for non-service units") {
  const auto body = kasli::tools::detail::format_systemd_unit_status_body(
      kasli::tools::detail::SystemdUnitStatusEvidence{
          .unit = "basic.target",
          .id = "basic.target",
          .description = "Basic System",
          .load_state = "loaded",
          .active_state = "active",
          .sub_state = "active",
          .unit_file_state = "static",
          .fragment_path = "/usr/lib/systemd/system/basic.target",
          .is_service = false,
      });

  REQUIRE(body.find("active_state=active") != std::string::npos);
  REQUIRE(body.find("service_result=") == std::string::npos);
  REQUIRE(body.find("exec_main_code=") == std::string::npos);
  REQUIRE(body.find("exec_main_status=") == std::string::npos);
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

TEST_CASE("systemd unit status tool requires unit") {
  kasli::tools::SystemdUnitStatusTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-status-missing-unit",
      .tool_name = "systemd.unit.status",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd.unit.status requires a non-empty unit parameter");
  REQUIRE(response.evidence.empty());
}

TEST_CASE("systemd unit status tool rejects empty unit") {
  kasli::tools::SystemdUnitStatusTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-systemd-status-empty-unit",
      .tool_name = "systemd.unit.status",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "  "}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "systemd.unit.status requires a non-empty unit parameter");
  REQUIRE(response.evidence.empty());
}
