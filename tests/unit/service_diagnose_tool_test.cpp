#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/service_diagnose_tool.hpp>

#include <optional>
#include <string>
#include <utility>

namespace {

class FixedTool final : public kasli::tools::Tool {
 public:
  FixedTool(std::string name,
            kasli::core::ToolStatus status,
            std::string message,
            std::string evidence_source,
            std::string evidence_body)
      : name_(std::move(name)),
        status_(status),
        message_(std::move(message)),
        evidence_source_(std::move(evidence_source)),
        evidence_body_(std::move(evidence_body)) {}

  std::string name() const override {
    return name_;
  }

  kasli::core::RiskClass risk() const override {
    return kasli::core::RiskClass::ReadOnly;
  }

  kasli::core::ToolResponse call(const kasli::core::ToolRequest& request) const override {
    ++calls_;
    last_request_ = request;

    std::vector<kasli::core::Evidence> evidence;
    if (!evidence_source_.empty()) {
      evidence.push_back(kasli::core::Evidence{
          .id = request.id + ':' + evidence_source_,
          .source = evidence_source_,
          .summary = "test evidence",
          .body = evidence_body_,
          .timestamp = "2026-04-30T00:00:00Z",
      });
    }

    return kasli::core::ToolResponse{
        .request_id = request.id,
        .status = status_,
        .message = message_,
        .evidence = std::move(evidence),
    };
  }

  int calls() const {
    return calls_;
  }

  const kasli::core::ToolRequest& last_request() const {
    return last_request_.value();
  }

 private:
  std::string name_;
  kasli::core::ToolStatus status_;
  std::string message_;
  std::string evidence_source_;
  std::string evidence_body_;
  mutable int calls_ = 0;
  mutable std::optional<kasli::core::ToolRequest> last_request_;
};

}  // namespace

TEST_CASE("service diagnose tool metadata is read-only") {
  FixedTool status("systemd.unit.status", kasli::core::ToolStatus::Ok, "ok", "", "");
  FixedTool journal("journal.query", kasli::core::ToolStatus::Ok, "ok", "", "");
  kasli::tools::ServiceDiagnoseTool tool(status, journal);

  REQUIRE(tool.name() == "service.diagnose");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("service diagnose requires unit") {
  FixedTool status("systemd.unit.status", kasli::core::ToolStatus::Ok, "ok", "", "");
  FixedTool journal("journal.query", kasli::core::ToolStatus::Ok, "ok", "", "");
  kasli::tools::ServiceDiagnoseTool tool(status, journal);

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-service-diagnose-missing",
      .tool_name = "service.diagnose",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "  "}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "service.diagnose requires a non-empty unit parameter");
  REQUIRE(response.evidence.empty());
  REQUIRE(status.calls() == 0);
  REQUIRE(journal.calls() == 0);
}

TEST_CASE("service diagnose aggregates failed service status and journal evidence") {
  FixedTool status("systemd.unit.status",
                   kasli::core::ToolStatus::Ok,
                   "systemd unit status read",
                   "systemd.unit.status",
                   "unit=sshd.service\n"
                   "load_state=loaded\n"
                   "active_state=failed\n"
                   "sub_state=failed\n"
                   "unit_file_state=enabled\n"
                   "fragment_path=/usr/lib/systemd/system/sshd.service\n"
                   "service_result=exit-code\n"
                   "exec_main_code=1\n"
                   "exec_main_status=255\n");
  FixedTool journal("journal.query",
                    kasli::core::ToolStatus::Ok,
                    "journal queried",
                    "journal.query",
                    "3 Failed to start OpenSSH server daemon\n"
                    "4 Bind to port 22 failed: Address already in use\n");
  kasli::tools::ServiceDiagnoseTool tool(status, journal);

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-service-diagnose",
      .tool_name = "service.diagnose",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "sshd.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "service diagnosis collected");
  REQUIRE(response.evidence.size() == 3);
  REQUIRE(response.evidence.at(0).source == "service.diagnose");
  REQUIRE(response.evidence.at(1).source == "systemd.unit.status");
  REQUIRE(response.evidence.at(2).source == "journal.query");

  const auto& body = response.evidence.at(0).body;
  REQUIRE(body.find("unit=sshd.service") != std::string::npos);
  REQUIRE(body.find("active_state=failed") != std::string::npos);
  REQUIRE(body.find("service_result=exit-code") != std::string::npos);
  REQUIRE(body.find("exec_main_status=255") != std::string::npos);
  REQUIRE(body.find("journal_entries_present=true") != std::string::npos);
  REQUIRE(body.find("journal_failure_terms_present=true") != std::string::npos);
  REQUIRE(body.find("diagnosis=service is failed") != std::string::npos);

  REQUIRE(status.calls() == 1);
  REQUIRE(journal.calls() == 1);
  REQUIRE(status.last_request().tool_name == "systemd.unit.status");
  REQUIRE(journal.last_request().tool_name == "journal.query");
  REQUIRE(status.last_request().params.at("unit") == "sshd.service");
  REQUIRE(journal.last_request().params.at("unit") == "sshd.service");
}

TEST_CASE("service diagnose returns partial success when journal is unavailable") {
  FixedTool status("systemd.unit.status",
                   kasli::core::ToolStatus::Ok,
                   "systemd unit status read",
                   "systemd.unit.status",
                   "unit=sshd.service\nactive_state=active\nsub_state=running\n");
  FixedTool journal("journal.query",
                    kasli::core::ToolStatus::Error,
                    "failed to open journal",
                    "",
                    "");
  kasli::tools::ServiceDiagnoseTool tool(status, journal);

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-service-diagnose-partial",
      .tool_name = "service.diagnose",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "sshd.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "service diagnosis collected");
  REQUIRE(response.evidence.size() == 2);
  REQUIRE(response.evidence.at(0).body.find("journal_tool_status=error") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("diagnosis=service is active") != std::string::npos);
}

TEST_CASE("service diagnose fails when status and journal both fail") {
  FixedTool status("systemd.unit.status",
                   kasli::core::ToolStatus::Error,
                   "failed to get systemd unit",
                   "",
                   "");
  FixedTool journal("journal.query",
                    kasli::core::ToolStatus::Error,
                    "failed to open journal",
                    "",
                    "");
  kasli::tools::ServiceDiagnoseTool tool(status, journal);

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-service-diagnose-failed",
      .tool_name = "service.diagnose",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "missing.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "service diagnosis failed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.find("status_tool_status=error") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("journal_tool_status=error") != std::string::npos);
}
