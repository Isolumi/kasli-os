#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/network_tool.hpp>

#include <string>
#include <vector>

TEST_CASE("network summary tool metadata is read-only") {
  kasli::tools::NetworkSummaryTool tool([] {
    return kasli::tools::detail::NetworkSummaryResult{.ok = true};
  });

  REQUIRE(tool.name() == "network.summary");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("network summary parses default route interface rows") {
  const auto interface = kasli::tools::detail::parse_default_route_interface(
      "wlp8s0\t00000000\t0102A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0");

  REQUIRE(interface.has_value());
  REQUIRE(*interface == "wlp8s0");
}

TEST_CASE("network summary ignores non-default and inactive route rows") {
  REQUIRE_FALSE(kasli::tools::detail::parse_default_route_interface(
                    "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT")
                    .has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_default_route_interface(
                    "wlp8s0\t0002A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0")
                    .has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_default_route_interface(
                    "wlp8s0\t00000000\t0102A8C0\t0003\t0\t0\t600\t00FFFFFF\t0\t0\t0")
                    .has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_default_route_interface(
                    "wlp8s0\t00000000\t0102A8C0\t0000\t0\t0\t600\t00000000\t0\t0\t0")
                    .has_value());
}

TEST_CASE("network summary formats address lists") {
  REQUIRE(kasli::tools::detail::format_network_address_list({}) == "none");
  REQUIRE(kasli::tools::detail::format_network_address_list({"192.168.1.20"}) ==
          "192.168.1.20");
  REQUIRE(kasli::tools::detail::format_network_address_list({"192.168.1.20", "10.0.0.2"}) ==
          "192.168.1.20,10.0.0.2");
}

TEST_CASE("network summary body is bounded and marks extra rows") {
  const auto body = kasli::tools::detail::format_network_summary_body(
      std::vector<kasli::tools::detail::NetworkInterfaceRow>{
          {
              .name = "wlp8s0",
              .oper_state = "up",
              .type = "1",
              .mtu = "1500",
              .carrier = "1",
              .mac_address = "aa:bb:cc:dd:ee:ff",
              .ipv4_addresses = {"192.168.1.20"},
              .ipv6_addresses = {"fe80::1"},
              .rx_bytes = 123,
              .tx_bytes = 456,
              .default_route = true,
              .source = "sysfs",
          },
          {
              .name = "lo",
              .oper_state = "unknown",
              .type = "772",
              .mtu = "65536",
              .carrier = "1",
              .mac_address = "00:00:00:00:00:00",
              .ipv4_addresses = {"127.0.0.1"},
              .ipv6_addresses = {"::1"},
              .rx_bytes = 10,
              .tx_bytes = 10,
              .default_route = false,
              .source = "sysfs",
          },
      },
      true);

  REQUIRE(body.find("network_interfaces_count=2") != std::string::npos);
  REQUIRE(body.find("name=wlp8s0 oper_state=up type=1 mtu=1500 carrier=1 "
                    "mac=aa:bb:cc:dd:ee:ff ipv4=192.168.1.20 ipv6=fe80::1 "
                    "rx_bytes=123 tx_bytes=456 default_route=true source=sysfs") !=
          std::string::npos);
  REQUIRE(body.find("name=lo oper_state=unknown type=772") != std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("network summary body reports when no interfaces are found") {
  const auto body = kasli::tools::detail::format_network_summary_body({}, false);

  REQUIRE(body.find("network_interfaces_count=0") != std::string::npos);
  REQUIRE(body.find("no_network_interfaces=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
}

TEST_CASE("network summary tool reads injected provider rows") {
  kasli::tools::NetworkSummaryTool tool([] {
    return kasli::tools::detail::NetworkSummaryResult{
        .ok = true,
        .rows = {kasli::tools::detail::NetworkInterfaceRow{
            .name = "wlp8s0",
            .oper_state = "up",
            .type = "1",
            .mtu = "1500",
            .carrier = "1",
            .mac_address = "aa:bb:cc:dd:ee:ff",
            .ipv4_addresses = {"192.168.1.20"},
            .ipv6_addresses = {"fe80::1"},
            .rx_bytes = 123,
            .tx_bytes = 456,
            .default_route = true,
            .source = "sysfs",
        }},
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-network-summary",
      .tool_name = "network.summary",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "network interfaces listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "network.summary");
  REQUIRE(response.evidence.front().body.find("network_interfaces_count=1") !=
          std::string::npos);
  REQUIRE(response.evidence.front().body.find("default_route=true") != std::string::npos);
}

TEST_CASE("network summary tool reports provider errors") {
  kasli::tools::NetworkSummaryTool tool([] {
    return kasli::tools::detail::NetworkSummaryResult{
        .ok = false,
        .error = "network interfaces are not available",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-network-summary-error",
      .tool_name = "network.summary",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "network interfaces are not available");
  REQUIRE(response.evidence.empty());
}
