#include <catch2/catch_test_macros.hpp>
#include <kasli/audit/audit_log.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include "../test_support/temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

nlohmann::json read_single_audit_event(const std::filesystem::path& path) {
  std::ifstream audit_file(path);
  std::string line;
  REQUIRE(std::getline(audit_file, line));

  std::string extra;
  REQUIRE_FALSE(std::getline(audit_file, extra));

  return nlohmann::json::parse(line);
}

class ThrowingTool final : public kasli::tools::Tool {
 public:
  std::string name() const override {
    return "throwing.tool";
  }

  kasli::core::RiskClass risk() const override {
    return kasli::core::RiskClass::ReadOnly;
  }

  kasli::core::ToolResponse call(const kasli::core::ToolRequest&) const override {
    throw std::runtime_error("tool exploded");
  }
};

void require_audit_event(const nlohmann::json& event,
                         const std::string& actor,
                         const std::string& summary,
                         const std::string& request_id,
                         const std::string& policy_decision,
                         const std::string& status) {
  REQUIRE(event.at("actor") == actor);
  REQUIRE(event.at("type") == "tool.call");
  REQUIRE(event.at("summary") == summary);
  REQUIRE(event.at("details").at("request_id") == request_id);
  REQUIRE(event.at("details").at("policy_decision") == policy_decision);
  REQUIRE(event.at("details").at("status") == status);
}

}  // namespace

TEST_CASE("session service executes allowed tool and audits it") {
  auto dir = kasli::test_support::temp_path("session_service_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
                                        .id = "req-1",
                                        .tool_name = "system.info",
                                        .risk = kasli::core::RiskClass::ReadOnly,
                                        .params = {},
                                    },
                                    "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);

  const auto event = read_single_audit_event(dir / "audit.jsonl");
  require_audit_event(event, "tester", "system.info", "req-1", "read-only tool allowed", "ok");
}

TEST_CASE("session service denies unknown tool and audits denial") {
  auto dir = kasli::test_support::temp_path("session_service_denial_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");
  kasli::tools::ToolRegistry registry;
  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
                                        .id = "req-2",
                                        .tool_name = "shell.exec",
                                        .risk = kasli::core::RiskClass::Admin,
                                        .params = {},
                                    },
                                    "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Denied);
  REQUIRE(response.message == "tool is not registered in policy allowlist");

  const auto event = read_single_audit_event(dir / "audit.jsonl");
  require_audit_event(event,
                      "tester",
                      "shell.exec",
                      "req-2",
                      "tool is not registered in policy allowlist",
                      "denied");
}

TEST_CASE("session service denies allowed policy tool missing from registry and audits denial") {
  auto dir = kasli::test_support::temp_path("session_service_registry_miss_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");
  kasli::tools::ToolRegistry registry;
  kasli::policy::PolicyBroker policy(
      {kasli::policy::ToolPolicy{"ghost.tool", kasli::core::RiskClass::ReadOnly}});
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
                                        .id = "req-3",
                                        .tool_name = "ghost.tool",
                                        .risk = kasli::core::RiskClass::ReadOnly,
                                        .params = {},
                                    },
                                    "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Denied);
  REQUIRE(response.message == "tool is not registered");

  const auto event = read_single_audit_event(dir / "audit.jsonl");
  require_audit_event(event, "tester", "ghost.tool", "req-3", "read-only tool allowed", "denied");
}

TEST_CASE("session service returns error and audits when tool throws") {
  auto dir = kasli::test_support::temp_path("session_service_exception_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<ThrowingTool>());

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
                                        .id = "req-4",
                                        .tool_name = "throwing.tool",
                                        .risk = kasli::core::RiskClass::ReadOnly,
                                        .params = {},
                                    },
                                    "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "tool exploded");

  const auto event = read_single_audit_event(dir / "audit.jsonl");
  require_audit_event(event, "tester", "throwing.tool", "req-4", "read-only tool allowed", "error");
}
