#pragma once

#include <kasli/tools/tool.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kasli::tools {

namespace detail {

struct NetworkInterfaceRow {
  std::string name;
  std::string oper_state;
  std::string type;
  std::string mtu;
  std::string carrier;
  std::string mac_address;
  std::vector<std::string> ipv4_addresses;
  std::vector<std::string> ipv6_addresses;
  std::uintmax_t rx_bytes = 0;
  std::uintmax_t tx_bytes = 0;
  bool default_route = false;
  std::string source;
};

struct NetworkSummaryResult {
  bool ok = false;
  std::string error;
  std::vector<NetworkInterfaceRow> rows;
  bool has_more_rows = false;
};

std::optional<std::string> parse_default_route_interface(std::string_view line);
std::string format_network_address_list(const std::vector<std::string>& addresses);
std::string format_network_summary_body(const std::vector<NetworkInterfaceRow>& rows,
                                        bool has_more_rows);

}  // namespace detail

using NetworkSummaryProvider = std::function<detail::NetworkSummaryResult()>;

NetworkSummaryProvider default_network_summary_provider();

class NetworkSummaryTool final : public Tool {
 public:
  explicit NetworkSummaryTool(NetworkSummaryProvider provider = default_network_summary_provider());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  NetworkSummaryProvider provider_;
};

}  // namespace kasli::tools
