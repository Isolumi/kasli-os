#include <catch2/catch_test_macros.hpp>
#include <kasli/ipc/line_protocol.hpp>

#include <nlohmann/json.hpp>

TEST_CASE("line protocol encodes tools list request") {
  auto request = kasli::ipc::make_tools_list_request("req-1");
  REQUIRE(request.at("id") == "req-1");
  REQUIRE(request.at("method") == "tools.list");
}

TEST_CASE("line protocol encodes tool call request") {
  kasli::core::ToolRequest call{
      .id = "tool-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  };

  auto request = kasli::ipc::make_tool_call_request("req-2", call);
  REQUIRE(request.at("method") == "tool.call");
  REQUIRE(request.at("tool").at("tool_name") == "system.info");
}

TEST_CASE("line protocol encodes ask request") {
  kasli::core::ToolRequest tool_request{
      .id = "tool-ask-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  };

  auto request = kasli::ipc::make_ask_request("req-ask-1", "What OS is this?", tool_request);
  REQUIRE(request.at("id") == "req-ask-1");
  REQUIRE(request.at("method") == "ask");
  REQUIRE(request.at("prompt") == "What OS is this?");
  REQUIRE(request.at("tool").at("id") == "tool-ask-1");
  REQUIRE(request.at("tool").at("tool_name") == "system.info");
}

TEST_CASE("line protocol maps successful tool response to ok envelope") {
  kasli::core::ToolResponse tool_response{
      .request_id = "tool-1",
      .status = kasli::core::ToolStatus::Ok,
      .message = "done",
      .evidence = {},
  };

  auto response = kasli::ipc::make_tool_call_response("req-3", tool_response);
  REQUIRE(response.at("id") == "req-3");
  REQUIRE(response.at("ok") == true);
  REQUIRE(response.at("response").at("status") == "ok");
}

TEST_CASE("line protocol maps denied tool response to failed envelope") {
  kasli::core::ToolResponse tool_response{
      .request_id = "tool-1",
      .status = kasli::core::ToolStatus::Denied,
      .message = "denied",
      .evidence = {},
  };

  auto response = kasli::ipc::make_tool_call_response("req-4", tool_response);
  REQUIRE(response.at("id") == "req-4");
  REQUIRE(response.at("ok") == false);
  REQUIRE(response.at("response").at("status") == "denied");
}

TEST_CASE("line protocol maps error tool response to failed envelope") {
  kasli::core::ToolResponse tool_response{
      .request_id = "tool-1",
      .status = kasli::core::ToolStatus::Error,
      .message = "failed",
      .evidence = {},
  };

  auto response = kasli::ipc::make_tool_call_response("req-5", tool_response);
  REQUIRE(response.at("id") == "req-5");
  REQUIRE(response.at("ok") == false);
  REQUIRE(response.at("response").at("status") == "error");
}

TEST_CASE("line protocol maps successful ask response to answer envelope") {
  auto response = kasli::ipc::make_ask_response("req-ask-2", true, "answer text", "");
  REQUIRE(response.at("id") == "req-ask-2");
  REQUIRE(response.at("ok") == true);
  REQUIRE(response.at("answer") == "answer text");
  REQUIRE_FALSE(response.contains("error"));
}

TEST_CASE("line protocol maps failed ask response to error envelope") {
  auto response = kasli::ipc::make_ask_response("req-ask-3", false, "", "model unavailable");
  REQUIRE(response.at("id") == "req-ask-3");
  REQUIRE(response.at("ok") == false);
  REQUIRE(response.at("error") == "model unavailable");
  REQUIRE_FALSE(response.contains("answer"));
}

TEST_CASE("line protocol preserves error response id") {
  auto response = kasli::ipc::make_error_response("req-6", "bad request");
  REQUIRE(response.at("id") == "req-6");
  REQUIRE(response.at("ok") == false);
  REQUIRE(response.at("error") == "bad request");
}
