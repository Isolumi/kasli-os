#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>
#include <kasli/core/uuid.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/ipc/unix_socket.hpp>
#include <kasli/model/ollama_provider.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/journal_tool.hpp>
#include <kasli/tools/systemd_tool.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

#ifndef KASLI_DEFAULT_JOURNAL_FIXTURE
#define KASLI_DEFAULT_JOURNAL_FIXTURE "tests/fixtures/journal/ssh_failed.jsonl"
#endif

struct Options {
  std::filesystem::path socket_path = "kaslid.sock";
  std::filesystem::path audit_path = "kasli-audit.jsonl";
  bool once = false;
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  bool socket_seen = false;
  bool audit_seen = false;

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--once") {
      if (options.once) {
        std::cerr << "--once may only be provided once\n";
        return std::nullopt;
      }
      options.once = true;
      continue;
    }

    if (arg == "--socket" || arg == "--audit-log") {
      bool& seen = (arg == "--socket") ? socket_seen : audit_seen;
      if (seen) {
        std::cerr << arg << " may only be provided once\n";
        return std::nullopt;
      }
      seen = true;

      if (index + 1 >= argc || std::string(argv[index + 1]).starts_with("--")) {
        std::cerr << arg << " requires a path\n";
        return std::nullopt;
      }

      if (arg == "--socket") {
        options.socket_path = argv[++index];
      } else {
        options.audit_path = argv[++index];
      }
      continue;
    }

    {
      std::cerr << "unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }

  return options;
}

std::string request_id_or_unknown(const nlohmann::json& request) {
  if (request.contains("id") && request.at("id").is_string()) {
    return request.at("id").get<std::string>();
  }
  return "unknown";
}

std::string bound_audit_string(const std::string& value) {
  constexpr std::size_t max_bytes = 2048;
  constexpr std::string_view marker = "...[truncated]";
  if (value.size() <= max_bytes) {
    return value;
  }
  return value.substr(0, max_bytes - marker.size()) + std::string(marker);
}

void audit_protocol_error(const kasli::audit::AuditLog& audit,
                          const std::string& request_id,
                          const std::string& message) {
  audit.append(kasli::core::AuditEvent{
      .id = kasli::core::make_event_id(),
      .timestamp = kasli::core::utc_timestamp(),
      .actor = "cli",
      .type = "protocol.error",
      .summary = bound_audit_string(message),
      .details = {{"request_id", bound_audit_string(request_id)}},
  });
}

std::filesystem::path journal_fixture_path() {
  if (const char* fixture = std::getenv("KASLI_JOURNAL_FIXTURE")) {
    return fixture;
  }
  return KASLI_DEFAULT_JOURNAL_FIXTURE;
}

std::string handle_request(const std::string& input,
                           const kasli::session::SessionService& service,
                           const kasli::audit::AuditLog& audit) {
  std::string id = "unknown";
  try {
    auto request = nlohmann::json::parse(input);
    id = request_id_or_unknown(request);
    if (id == "unknown") {
      throw std::invalid_argument("request id is required");
    }

    const std::string method = request.at("method").get<std::string>();

    if (method == "tools.list") {
      return nlohmann::json{{"id", id}, {"ok", true}, {"tools", service.list_tools()}}.dump();
    }

    if (method == "tool.call") {
      auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
      auto response = service.call_tool(tool_request, "cli");
      return kasli::ipc::make_tool_call_response(id, response).dump();
    }

    if (method == "ask") {
      const std::string prompt = request.at("prompt").get<std::string>();
      auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
      kasli::model::OllamaProvider model("http://127.0.0.1:11434", "llama3.2");
      const auto result = service.ask_with_tool(prompt, tool_request, model, "cli");
      return kasli::ipc::make_ask_response(id, result.ok, result.answer, result.error).dump();
    }

    audit_protocol_error(audit, id, "unknown method");
    return kasli::ipc::make_error_response(id, "unknown method").dump();
  } catch (const std::exception& error) {
    audit_protocol_error(audit, id, error.what());
    return kasli::ipc::make_error_response(id, error.what()).dump();
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parse_options(argc, argv);
  if (!options.has_value()) {
    return 2;
  }

  try {
    kasli::tools::ToolRegistry registry;
    registry.add(std::make_unique<kasli::tools::SystemInfoTool>());
    registry.add(std::make_unique<kasli::tools::SystemdUnitsTool>());
    registry.add(std::make_unique<kasli::tools::SystemdUnitStatusTool>());
#if KASLI_HAS_SYSTEMD
    registry.add(std::make_unique<kasli::tools::LiveJournalTool>());
#else
    registry.add(std::make_unique<kasli::tools::JournalFixtureTool>(journal_fixture_path()));
#endif

    kasli::policy::PolicyBroker policy(registry.policies());
    kasli::audit::AuditLog audit(options->audit_path);
    kasli::session::SessionService service(registry, policy, audit);
    kasli::ipc::UnixSocketServer server(options->socket_path);

    while (true) {
      try {
        server.accept_one([&](const std::string& request) {
          return handle_request(request, service, audit);
        });
        if (options->once) {
          break;
        }
      } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (options->once) {
          return 1;
        }
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
