#include <catch2/catch_test_macros.hpp>
#include <kasli/audit/audit_log.hpp>
#include <kasli/model/model_provider.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include "../test_support/fake_model_provider.hpp"
#include "../test_support/temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class EvidenceTool final : public kasli::tools::Tool {
 public:
  std::string name() const override {
    return "journal.query";
  }

  kasli::core::RiskClass risk() const override {
    return kasli::core::RiskClass::ReadOnly;
  }

  kasli::core::ToolResponse call(const kasli::core::ToolRequest& request) const override {
    return kasli::core::ToolResponse{
        .request_id = request.id,
        .status = kasli::core::ToolStatus::Ok,
        .message = "journal evidence",
        .evidence = {kasli::core::Evidence{
            .id = "ev-1",
            .source = "journal.query",
            .summary = "ssh journal",
            .body = "Bind to port 22 failed",
            .timestamp = "",
        }},
    };
  }
};

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

class ThrowingModelProvider final : public kasli::model::ModelProvider {
 public:
  std::string complete(const kasli::model::ModelRequest&) const override {
    ++calls;
    throw std::runtime_error("model exploded");
  }

  mutable int calls = 0;
};

std::vector<nlohmann::json> read_audit_events(const std::filesystem::path& path) {
  std::ifstream audit_file(path);
  std::vector<nlohmann::json> events;
  std::string line;
  while (std::getline(audit_file, line)) {
    events.push_back(nlohmann::json::parse(line));
  }
  return events;
}

}  // namespace

TEST_CASE("fake model receives curated evidence only") {
  kasli::test_support::FakeModelProvider provider;
  kasli::model::ModelRequest request{
      .prompt = "Why did ssh fail?",
      .evidence = {kasli::core::Evidence{
          .id = "ev-1",
          .source = "journal.query",
          .summary = "ssh journal",
          .body = "Bind to port 22 failed",
          .timestamp = "",
      }},
  };

  REQUIRE(provider.complete(request) == "Fake answer based on 1 evidence record(s).");
  REQUIRE(provider.calls == 1);
}

TEST_CASE("ask with tool sends tool evidence to model and audits model request") {
  auto dir = kasli::test_support::temp_path("model_context_ask_with_tool_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<EvidenceTool>());

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);
  kasli::test_support::FakeModelProvider provider;

  auto result = service.ask_with_tool("Why did ssh fail?",
                                      kasli::core::ToolRequest{
                                          .id = "req-ask-1",
                                          .tool_name = "journal.query",
                                          .risk = kasli::core::RiskClass::ReadOnly,
                                          .params = {},
                                      },
                                      provider,
                                      "tester");

  REQUIRE(result.ok);
  REQUIRE(result.answer == "Fake answer based on 1 evidence record(s).");
  REQUIRE(result.error.empty());
  REQUIRE(provider.calls == 1);

  const auto events = read_audit_events(dir / "audit.jsonl");
  REQUIRE(events.size() == 3);
  REQUIRE(events[0].at("type") == "tool.call");
  REQUIRE(events[1].at("type") == "model.request");
  REQUIRE(events[2].at("type") == "model.response");
  REQUIRE_FALSE(events[0].at("timestamp").get<std::string>().empty());
  REQUIRE_FALSE(events[1].at("timestamp").get<std::string>().empty());
  REQUIRE_FALSE(events[2].at("timestamp").get<std::string>().empty());
  REQUIRE(events[1].at("actor") == "tester");
  REQUIRE(events[1].at("summary") == "Why did ssh fail?");
  REQUIRE(events[1].at("details").at("request_id") == "req-ask-1");
  REQUIRE(events[1].at("details").at("prompt") == "Why did ssh fail?");
  REQUIRE(events[1].at("details").at("evidence_count") == "1");
  const auto request_refs =
      nlohmann::json::parse(events[1].at("details").at("evidence_refs").get<std::string>());
  REQUIRE(request_refs.at(0).at("id") == "ev-1");
  REQUIRE(request_refs.at(0).at("source") == "journal.query");
  REQUIRE_FALSE(request_refs.at(0).contains("body"));
  REQUIRE(events[2].at("details").at("request_id") == "req-ask-1");
  REQUIRE(events[2].at("details").at("response_status") == "ok");
  REQUIRE(events[2].at("details").at("response_message") == "model response received");
  REQUIRE(events[2].at("details").at("response_bytes") ==
          std::to_string(std::string("Fake answer based on 1 evidence record(s).").size()));
  REQUIRE(events[2].at("details").at("response_preview") ==
          "Fake answer based on 1 evidence record(s).");
}

TEST_CASE("ask with denied tool skips model request") {
  auto dir = kasli::test_support::temp_path("model_context_denied_tool_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);
  kasli::test_support::FakeModelProvider provider;

  auto result = service.ask_with_tool("Why did ssh fail?",
                                      kasli::core::ToolRequest{
                                          .id = "req-denied-1",
                                          .tool_name = "unknown.tool",
                                          .risk = kasli::core::RiskClass::ReadOnly,
                                          .params = {},
                                      },
                                      provider,
                                      "tester");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.answer.empty());
  REQUIRE(result.error == "tool call did not succeed: tool is not registered in policy allowlist");
  REQUIRE(provider.calls == 0);

  const auto events = read_audit_events(dir / "audit.jsonl");
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].at("type") == "tool.call");
  REQUIRE(events[1].at("type") == "model.skipped");
  REQUIRE_FALSE(events[1].at("timestamp").get<std::string>().empty());
  REQUIRE(events[1].at("details").at("request_id") == "req-denied-1");
  REQUIRE(events[1].at("details").at("prompt") == "Why did ssh fail?");
  REQUIRE(events[1].at("details").at("tool_status") == "denied");
  REQUIRE(events[1].at("details").at("reason") == "tool is not registered in policy allowlist");
  REQUIRE(events[1].at("details").at("evidence_count") == "0");
}

TEST_CASE("ask with tool error skips model request") {
  auto dir = kasli::test_support::temp_path("model_context_tool_error_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<ThrowingTool>());

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);
  kasli::test_support::FakeModelProvider provider;

  auto result = service.ask_with_tool("Why did ssh fail?",
                                      kasli::core::ToolRequest{
                                          .id = "req-error-1",
                                          .tool_name = "throwing.tool",
                                          .risk = kasli::core::RiskClass::ReadOnly,
                                          .params = {},
                                      },
                                      provider,
                                      "tester");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.answer.empty());
  REQUIRE(result.error == "tool call did not succeed: tool exploded");
  REQUIRE(provider.calls == 0);

  const auto events = read_audit_events(dir / "audit.jsonl");
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].at("type") == "tool.call");
  REQUIRE(events[1].at("type") == "model.skipped");
  REQUIRE_FALSE(events[1].at("timestamp").get<std::string>().empty());
  REQUIRE(events[1].at("details").at("request_id") == "req-error-1");
  REQUIRE(events[1].at("details").at("prompt") == "Why did ssh fail?");
  REQUIRE(events[1].at("details").at("tool_status") == "error");
  REQUIRE(events[1].at("details").at("reason") == "tool exploded");
  REQUIRE(events[1].at("details").at("evidence_count") == "0");
}

TEST_CASE("ask with model error returns failed result and audits model error") {
  auto dir = kasli::test_support::temp_path("model_context_model_error_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<EvidenceTool>());

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::session::SessionService service(registry, policy, audit);
  ThrowingModelProvider provider;

  auto result = service.ask_with_tool("Why did ssh fail?",
                                      kasli::core::ToolRequest{
                                          .id = "req-model-error-1",
                                          .tool_name = "journal.query",
                                          .risk = kasli::core::RiskClass::ReadOnly,
                                          .params = {},
                                      },
                                      provider,
                                      "tester");

  REQUIRE_FALSE(result.ok);
  REQUIRE(result.answer.empty());
  REQUIRE(result.error == "model exploded");
  REQUIRE(provider.calls == 1);

  const auto events = read_audit_events(dir / "audit.jsonl");
  REQUIRE(events.size() == 3);
  REQUIRE(events[0].at("type") == "tool.call");
  REQUIRE(events[1].at("type") == "model.request");
  REQUIRE(events[2].at("type") == "model.error");
  REQUIRE_FALSE(events[2].at("timestamp").get<std::string>().empty());
  REQUIRE(events[2].at("details").at("request_id") == "req-model-error-1");
  REQUIRE(events[2].at("details").at("prompt") == "Why did ssh fail?");
  REQUIRE(events[2].at("details").at("error") == "model exploded");
  REQUIRE(events[2].at("details").at("evidence_count") == "1");
}
