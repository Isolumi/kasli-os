#include <kasli/ipc/line_protocol.hpp>

namespace kasli::ipc {

nlohmann::json make_tools_list_request(const std::string& request_id) {
  return nlohmann::json{{"id", request_id}, {"method", "tools.list"}};
}

nlohmann::json make_tool_call_request(const std::string& request_id,
                                      const core::ToolRequest& tool_request) {
  return nlohmann::json{{"id", request_id}, {"method", "tool.call"}, {"tool", tool_request}};
}

nlohmann::json make_ask_request(const std::string& request_id,
                                const std::string& prompt,
                                const core::ToolRequest& tool_request) {
  return nlohmann::json{
      {"id", request_id},
      {"method", "ask"},
      {"prompt", prompt},
      {"tool", tool_request},
  };
}

nlohmann::json make_tool_call_response(const std::string& request_id,
                                       const core::ToolResponse& tool_response) {
  return nlohmann::json{
      {"id", request_id},
      {"ok", tool_response.status == core::ToolStatus::Ok},
      {"response", tool_response},
  };
}

nlohmann::json make_ask_response(const std::string& request_id,
                                 bool ok,
                                 const std::string& answer,
                                 const std::string& error) {
  auto response = nlohmann::json{{"id", request_id}, {"ok", ok}};
  if (ok) {
    response["answer"] = answer;
  } else {
    response["error"] = error;
  }
  return response;
}

nlohmann::json make_error_response(const std::string& request_id, const std::string& message) {
  return nlohmann::json{{"id", request_id}, {"ok", false}, {"error", message}};
}

}  // namespace kasli::ipc
