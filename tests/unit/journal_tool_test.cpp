#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/journal_tool.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path write_fixture(const std::string& name, const std::string& contents) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream output(path);
  output << contents;
  return path;
}

}  // namespace

TEST_CASE("journal fixture tool filters unit and redacts secrets") {
  kasli::tools::JournalFixtureTool tool("tests/fixtures/journal/ssh_failed.jsonl");
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-1",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.find("Address already in use") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("should_not_leave_context") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("[REDACTED]") != std::string::npos);
}

TEST_CASE("journal fixture tool reports missing fixture") {
  const auto fixture = std::filesystem::temp_directory_path() / "kasli-missing-journal-fixture.jsonl";
  std::filesystem::remove(fixture);
  kasli::tools::JournalFixtureTool tool(fixture);
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-missing",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message.find("failed to open journal fixture: ") == 0);
  REQUIRE(response.evidence.empty());
}

TEST_CASE("journal fixture tool skips malformed rows") {
  const auto fixture = write_fixture(
      "kasli-journal-malformed.jsonl",
      "not json\n"
      "[]\n"
      "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"4\",\"MESSAGE\":\"password: hunter2\"}\n");

  kasli::tools::JournalFixtureTool tool(fixture);
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-malformed",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.find("skipped_invalid_entries=2") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("password: [REDACTED]") != std::string::npos);
}

TEST_CASE("journal fixture tool returns ok evidence for no matches") {
  kasli::tools::JournalFixtureTool tool("tests/fixtures/journal/ssh_failed.jsonl");
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-none",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "missing.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.empty());
}

TEST_CASE("journal fixture tool truncates bounded evidence") {
  std::string fixture_contents;
  for (int index = 0; index < 55; ++index) {
    fixture_contents +=
        "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"6\",\"MESSAGE\":\"entry " +
        std::to_string(index) + "\"}\n";
  }
  const auto fixture = write_fixture("kasli-journal-many.jsonl", fixture_contents);

  kasli::tools::JournalFixtureTool tool(fixture);
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-truncated",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.find("entry 49") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("entry 50") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("truncated=true") != std::string::npos);
}

TEST_CASE("journal fixture tool caps body bytes") {
  const auto fixture = write_fixture(
      "kasli-journal-large.jsonl",
      "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"6\",\"MESSAGE\":\"" +
          std::string(20 * 1024, 'x') + "\"}\n");

  kasli::tools::JournalFixtureTool tool(fixture);
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-large",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.size() <= 16 * 1024);
  REQUIRE(response.evidence.at(0).body.find("truncated=true") != std::string::npos);
}

TEST_CASE("journal fixture tool redacts common journal secret forms") {
  const auto fixture = write_fixture(
      "kasli-journal-secrets.jsonl",
      "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"4\",\"MESSAGE\":\"password: hunter2\"}\n"
      "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"4\",\"MESSAGE\":\"Authorization: Bearer abc123\"}\n"
      "{\"_SYSTEMD_UNIT\":\"ssh.service\",\"PRIORITY\":\"4\",\"MESSAGE\":\"token=\\\"abc def\\\"\"}\n");

  kasli::tools::JournalFixtureTool tool(fixture);
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-secrets",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.at(0).body.find("hunter2") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("abc123") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("abc def") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("password: [REDACTED]") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("Authorization: Bearer [REDACTED]") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("token=\"[REDACTED]\"") != std::string::npos);
}

TEST_CASE("live journal tool reports unavailable when systemd is not built") {
#if !KASLI_HAS_SYSTEMD
  kasli::tools::LiveJournalTool tool;
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-live-journal-unavailable",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "journal support was not built");
  REQUIRE(response.evidence.empty());
#endif
}
