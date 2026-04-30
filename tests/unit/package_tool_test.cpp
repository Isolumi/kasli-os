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

TEST_CASE("package list tool metadata is read-only") {
  kasli::tools::PackageListTool tool([] {
    return kasli::tools::detail::PackageCommandResult{.ok = true};
  });

  REQUIRE(tool.name() == "packages.list");
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

TEST_CASE("package list parser extracts rpm query rows") {
  const auto row = kasli::tools::detail::parse_rpm_package_list_line(
      "openssl\t1\t3.5.4\t3.fc43\tx86_64\t1761191373",
      "rpmdb");

  REQUIRE(row.has_value());
  REQUIRE(row->name == "openssl");
  REQUIRE(row->epoch == "1");
  REQUIRE(row->version == "3.5.4");
  REQUIRE(row->release == "3.fc43");
  REQUIRE(row->arch == "x86_64");
  REQUIRE(row->install_time == "1761191373");
  REQUIRE(row->source == "rpmdb");
}

TEST_CASE("package list parser ignores malformed rpm query rows") {
  REQUIRE_FALSE(
      kasli::tools::detail::parse_rpm_package_list_line("openssl\t1\t3.5.4", "rpmdb")
          .has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_rpm_package_list_line(
                    "\t1\t3.5.4\t3.fc43\tx86_64\t1761191373",
                    "rpmdb")
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

TEST_CASE("package list body is bounded and marks extra rows") {
  const auto body = kasli::tools::detail::format_package_list_body(
      std::vector<kasli::tools::detail::PackageListRow>{
          {
              .name = "openssl",
              .epoch = "1",
              .version = "3.5.4",
              .release = "3.fc43",
              .arch = "x86_64",
              .install_time = "1761191373",
              .source = "rpmdb",
          },
          {
              .name = "filesystem",
              .epoch = "0",
              .version = "3.18",
              .release = "50.fc43",
              .arch = "x86_64",
              .install_time = "1761191373",
              .source = "rpmdb",
          },
      },
      true);

  REQUIRE(body.find("packages_count=2") != std::string::npos);
  REQUIRE(body.find("name=openssl epoch=1 version=3.5.4 release=3.fc43 arch=x86_64 "
                    "install_time=1761191373 source=rpmdb") != std::string::npos);
  REQUIRE(body.find("name=filesystem epoch=0 version=3.18 release=50.fc43 arch=x86_64") !=
          std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("package list body reports when no packages are found") {
  const auto body = kasli::tools::detail::format_package_list_body({}, false);

  REQUIRE(body.find("packages_count=0") != std::string::npos);
  REQUIRE(body.find("no_packages=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
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

TEST_CASE("package list tool reads injected rpm output") {
  kasli::tools::PackageListTool tool([] {
    return kasli::tools::detail::PackageCommandResult{
        .ok = true,
        .exit_code = 0,
        .output =
            "openssl\t1\t3.5.4\t3.fc43\tx86_64\t1761191373\n"
            "filesystem\t0\t3.18\t50.fc43\tx86_64\t1761191373\n",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-packages-list",
      .tool_name = "packages.list",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "installed packages listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "packages.list");
  REQUIRE(response.evidence.front().body.find("packages_count=2") != std::string::npos);
  REQUIRE(response.evidence.front().body.find("name=openssl") != std::string::npos);
}

TEST_CASE("package list tool reports command failures") {
  kasli::tools::PackageListTool tool([] {
    return kasli::tools::detail::PackageCommandResult{
        .ok = false,
        .exit_code = 127,
        .error = "rpm executable not found",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-packages-list-failure",
      .tool_name = "packages.list",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "rpm executable not found");
  REQUIRE(response.evidence.empty());
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
