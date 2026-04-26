#pragma once

#include <kasli/core/types.hpp>

#include <nlohmann/json.hpp>
#include <string>

namespace kasli::ipc {

nlohmann::json make_tools_list_request(const std::string& request_id);
nlohmann::json make_tool_call_request(const std::string& request_id,
                                      const core::ToolRequest& tool_request);
nlohmann::json make_ask_request(const std::string& request_id,
                                const std::string& prompt,
                                const core::ToolRequest& tool_request);
nlohmann::json make_tool_call_response(const std::string& request_id,
                                       const core::ToolResponse& tool_response);
nlohmann::json make_ask_response(const std::string& request_id,
                                 bool ok,
                                 const std::string& answer,
                                 const std::string& error);
nlohmann::json make_error_response(const std::string& request_id, const std::string& message);

}  // namespace kasli::ipc
