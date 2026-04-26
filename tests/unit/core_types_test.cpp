#include <catch2/catch_test_macros.hpp>
#include <kasli/core/types.hpp>

#include <nlohmann/json.hpp>

using kasli::core::Evidence;
using kasli::core::RiskClass;
using kasli::core::ToolRequest;
using kasli::core::ToolStatus;

TEST_CASE("risk class converts to and from strings") {
  REQUIRE(kasli::core::to_string(RiskClass::ReadOnly) == "read_only");
  REQUIRE(kasli::core::risk_class_from_string("read_only") == RiskClass::ReadOnly);
  REQUIRE(kasli::core::risk_class_from_string("admin") == RiskClass::Admin);
}

TEST_CASE("tool request serializes with typed risk") {
  ToolRequest request;
  request.id = "req-1";
  request.tool_name = "system.info";
  request.risk = RiskClass::ReadOnly;
  request.params = {{"format", "summary"}};

  nlohmann::json encoded = request;
  REQUIRE(encoded.at("id") == "req-1");
  REQUIRE(encoded.at("tool_name") == "system.info");
  REQUIRE(encoded.at("risk") == "read_only");
  REQUIRE(encoded.at("params").at("format") == "summary");

  ToolRequest decoded = encoded.get<ToolRequest>();
  REQUIRE(decoded.id == request.id);
  REQUIRE(decoded.tool_name == request.tool_name);
  REQUIRE(decoded.risk == request.risk);
  REQUIRE(decoded.params.at("format") == "summary");
}

TEST_CASE("tool response carries evidence records") {
  kasli::core::ToolResponse response;
  response.request_id = "req-1";
  response.status = ToolStatus::Ok;
  response.message = "system inspected";
  response.evidence.push_back(Evidence{
      .id = "ev-1",
      .source = "system.info",
      .summary = "Linux host",
      .body = "kernel=6.8",
      .timestamp = "2026-04-26T00:00:00Z",
  });

  nlohmann::json encoded = response;
  REQUIRE(encoded.at("status") == "ok");
  REQUIRE(encoded.at("evidence").at(0).at("id") == "ev-1");
}
