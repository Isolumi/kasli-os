#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>
#include <kasli/core/uuid.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

namespace {

std::optional<std::filesystem::path> parse_audit_path(int argc, char** argv) {
  std::optional<std::filesystem::path> audit_path;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg != "--audit-log") {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }

    if (audit_path.has_value()) {
      std::cerr << "--audit-log may only be provided once\n";
      return std::nullopt;
    }

    if (index + 1 >= argc || std::string(argv[index + 1]).starts_with("--")) {
      std::cerr << "--audit-log requires a path\n";
      return std::nullopt;
    }

    audit_path = argv[++index];
  }

  return audit_path.value_or(std::filesystem::path{"kasli-audit.jsonl"});
}

std::string request_id_or_unknown(const nlohmann::json& request) {
  if (request.contains("id") && request.at("id").is_string()) {
    return request.at("id").get<std::string>();
  }
  return "unknown";
}

void audit_protocol_error(const kasli::audit::AuditLog& audit,
                          const std::string& request_id,
                          const std::string& message) {
  audit.append(kasli::core::AuditEvent{
      .id = kasli::core::make_event_id(),
      .timestamp = "",
      .actor = "cli",
      .type = "protocol.error",
      .summary = message,
      .details = {{"request_id", request_id}},
  });
}

}  // namespace

int main(int argc, char** argv) {
  auto audit_path = parse_audit_path(argc, argv);
  if (!audit_path.has_value()) {
    return 2;
  }

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>());

  kasli::policy::PolicyBroker policy(registry.policies());
  kasli::audit::AuditLog audit(*audit_path);
  kasli::session::SessionService service(registry, policy, audit);

  std::string line;
  while (std::getline(std::cin, line)) {
    std::string id = "unknown";
    try {
      auto request = nlohmann::json::parse(line);
      id = request_id_or_unknown(request);
      if (id == "unknown") {
        throw std::invalid_argument("request id is required");
      }

      const std::string method = request.at("method").get<std::string>();

      if (method == "tools.list") {
        std::cout << nlohmann::json{{"id", id}, {"ok", true}, {"tools", service.list_tools()}}.dump()
                  << '\n';
        continue;
      }

      if (method == "tool.call") {
        auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
        auto response = service.call_tool(tool_request, "cli");
        std::cout << kasli::ipc::make_tool_call_response(id, response).dump() << '\n';
        continue;
      }

      audit_protocol_error(audit, id, "unknown method");
      std::cout << kasli::ipc::make_error_response(id, "unknown method").dump() << '\n';
    } catch (const std::exception& error) {
      audit_protocol_error(audit, id, error.what());
      std::cout << kasli::ipc::make_error_response(id, error.what()).dump() << '\n';
    }
  }

  return 0;
}
