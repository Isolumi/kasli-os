#include <CLI/CLI.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{"Kasli read-only system assistant CLI"};

  std::string socket_path = "kaslid.sock";
  bool list_tools = false;
  std::string call_tool;
  app.add_option("--socket", socket_path, "Path to the kaslid Unix domain socket");
  auto* tools_list_option = app.add_flag("--tools-list", list_tools, "Request the daemon's tool list");
  auto* call_tool_option =
      app.add_option("--call-tool", call_tool, "Request a read-only tool call from the daemon");
  tools_list_option->excludes(call_tool_option);

  CLI11_PARSE(app, argc, argv);

  std::string request;
  if (list_tools) {
    request = kasli::ipc::make_tools_list_request("cli-1").dump();
  } else if (!call_tool.empty()) {
    kasli::core::ToolRequest tool_request{
        .id = "cli-tool-1",
        .tool_name = call_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    };
    request = kasli::ipc::make_tool_call_request("cli-2", tool_request).dump();
  } else {
    std::cerr << app.help() << '\n';
    return 1;
  }

  try {
    std::cout << kasli::ipc::request_over_unix_socket(socket_path, request) << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
