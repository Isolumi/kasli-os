#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/package_tool.hpp>

#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("package recent changes tool metadata is read-only") {
  kasli::tools::PackageRecentChangesTool tool(
      {std::filesystem::path{"tests/fixtures/packages/dnf5.log"}});

  REQUIRE(tool.name() == "packages.recent_changes");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("package recent changes parser extracts dnf5 rpm callback starts") {
  const auto row = kasli::tools::detail::parse_package_change_log_line(
      "2026-04-30T03:56:52+0000 [18165] INFO RPM callback install start "
      "\"cmake-rpm-macros-0:3.31.11-1.fc43.noarch\" total 9084",
      "/var/log/dnf5.log");

  REQUIRE(row.has_value());
  REQUIRE(row->timestamp == "2026-04-30T03:56:52+0000");
  REQUIRE(row->action == "install");
  REQUIRE(row->package == "cmake-rpm-macros-0:3.31.11-1.fc43.noarch");
  REQUIRE(row->source == "/var/log/dnf5.log");
}

TEST_CASE("package recent changes parser ignores dnf5 scriptlets and stop callbacks") {
  REQUIRE_FALSE(kasli::tools::detail::parse_package_change_log_line(
                    "2026-04-30T03:56:52+0000 [18165] INFO RPM callback install stop "
                    "\"cmake-rpm-macros-0:3.31.11-1.fc43.noarch\" amount 9084 total 9084",
                    "/var/log/dnf5.log")
                    .has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_package_change_log_line(
                    "2026-04-30T16:33:38+0000 [7433] INFO RPM callback start %post "
                    "scriptlet \"openssh-0:10.0p1-9.fc43.x86_64\"",
                    "/var/log/dnf5.log")
                    .has_value());
}

TEST_CASE("package recent changes body is bounded and marks extra rows") {
  const auto body = kasli::tools::detail::format_package_recent_changes_body(
      std::vector<kasli::tools::detail::PackageChangeRow>{
          {
              .timestamp = "2026-04-30T16:33:38+0000",
              .action = "install",
              .package = "openssh-0:10.0p1-9.fc43.x86_64",
              .source = "/var/log/dnf5.log",
          },
          {
              .timestamp = "2026-04-30T03:56:52+0000",
              .action = "install",
              .package = "cmake-rpm-macros-0:3.31.11-1.fc43.noarch",
              .source = "/var/log/dnf5.log",
          },
      },
      true);

  REQUIRE(body.find("package_changes_count=2") != std::string::npos);
  REQUIRE(body.find("action=install package=openssh-0:10.0p1-9.fc43.x86_64") !=
          std::string::npos);
  REQUIRE(body.find("source=/var/log/dnf5.log") != std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("package recent changes body reports when no changes are found") {
  const auto body = kasli::tools::detail::format_package_recent_changes_body({}, false);

  REQUIRE(body.find("package_changes_count=0") != std::string::npos);
  REQUIRE(body.find("no_package_changes=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
}

TEST_CASE("package recent changes tool reads fixture logs") {
  kasli::tools::PackageRecentChangesTool tool(
      {std::filesystem::path{"tests/fixtures/packages/dnf5.log"}});

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-packages-recent",
      .tool_name = "packages.recent_changes",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "recent package changes listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "packages.recent_changes");
  REQUIRE(response.evidence.front().body.find("package_changes_count=2") != std::string::npos);
  REQUIRE(response.evidence.front().body.find("openssh-0:10.0p1-9.fc43.x86_64") !=
          std::string::npos);
}

TEST_CASE("package recent changes tool reports missing logs") {
  kasli::tools::PackageRecentChangesTool tool(
      {std::filesystem::path{"tests/fixtures/packages/missing.log"}});

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-packages-missing",
      .tool_name = "packages.recent_changes",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "package history logs not found");
  REQUIRE(response.evidence.empty());
}
