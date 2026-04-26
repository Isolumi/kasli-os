#include <CLI/CLI.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>

#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{"Kasli read-only system assistant CLI"};

  bool list_tools = false;
  std::string call_tool;
  app.add_flag("--tools-list", list_tools, "Print a tools.list JSON request");
  app.add_option("--call-tool", call_tool, "Print a tool.call JSON request for a read-only tool");

  CLI11_PARSE(app, argc, argv);

  if (list_tools) {
    std::cout << kasli::ipc::make_tools_list_request("cli-1").dump() << '\n';
    return 0;
  }

  if (!call_tool.empty()) {
    kasli::core::ToolRequest request{
        .id = "cli-tool-1",
        .tool_name = call_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    };
    std::cout << kasli::ipc::make_tool_call_request("cli-2", request).dump() << '\n';
    return 0;
  }

  std::cerr << app.help() << '\n';
  return 1;
}
