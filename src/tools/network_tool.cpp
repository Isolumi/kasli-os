#include <kasli/tools/network_tool.hpp>

#include <kasli/core/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <utility>
#include <vector>

#include <ifaddrs.h>

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxNetworkRows = 100;
constexpr std::size_t kMaxAddressesPerFamily = 8;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;

void append_bounded(std::string& body, const std::string& text, bool& truncated) {
  if (body.size() + text.size() <= kMaxBodyBytes) {
    body += text;
    return;
  }

  const std::string marker = "truncated=true\n";
  const auto remaining = kMaxBodyBytes - body.size();
  if (remaining > marker.size()) {
    body += text.substr(0, remaining - marker.size());
  } else if (body.size() + marker.size() > kMaxBodyBytes) {
    body.resize(kMaxBodyBytes - marker.size());
  }
  body += marker;
  truncated = true;
}

std::vector<std::string_view> split_whitespace(std::string_view value) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start < value.size()) {
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
      ++start;
    }
    if (start >= value.size()) {
      break;
    }

    std::size_t end = start;
    while (end < value.size() && value[end] != ' ' && value[end] != '\t') {
      ++end;
    }
    fields.push_back(value.substr(start, end - start));
    start = end;
  }
  return fields;
}

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                           value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    value.erase(0, start);
  }
  return value;
}

std::string read_text_file(const std::filesystem::path& path, std::string fallback = "unknown") {
  std::ifstream input(path);
  if (!input) {
    return fallback;
  }
  std::string value;
  std::getline(input, value);
  value = trim(value);
  return value.empty() ? fallback : value;
}

std::uintmax_t read_uint_file(const std::filesystem::path& path) {
  const auto value = read_text_file(path, "0");
  try {
    return static_cast<std::uintmax_t>(std::stoull(value));
  } catch (...) {
    return 0;
  }
}

void add_bounded_unique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty() || values.size() >= kMaxAddressesPerFamily) {
    return;
  }
  if (std::ranges::find(values, value) == values.end()) {
    values.push_back(value);
  }
}

struct InterfaceAddresses {
  std::vector<std::string> ipv4;
  std::vector<std::string> ipv6;
};

std::map<std::string, InterfaceAddresses> collect_interface_addresses() {
  std::map<std::string, InterfaceAddresses> addresses;

  ifaddrs* raw_addresses = nullptr;
  if (getifaddrs(&raw_addresses) != 0) {
    return addresses;
  }

  for (auto* current = raw_addresses; current != nullptr; current = current->ifa_next) {
    if (current->ifa_name == nullptr || current->ifa_addr == nullptr) {
      continue;
    }

    const int family = current->ifa_addr->sa_family;
    if (family != AF_INET && family != AF_INET6) {
      continue;
    }

    char host[NI_MAXHOST] = {};
    const auto length = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    const int result = getnameinfo(current->ifa_addr,
                                   static_cast<socklen_t>(length),
                                   host,
                                   sizeof(host),
                                   nullptr,
                                   0,
                                   NI_NUMERICHOST);
    if (result != 0) {
      continue;
    }

    auto& item = addresses[current->ifa_name];
    if (family == AF_INET) {
      add_bounded_unique(item.ipv4, host);
    } else {
      add_bounded_unique(item.ipv6, host);
    }
  }

  freeifaddrs(raw_addresses);
  return addresses;
}

std::set<std::string> collect_default_route_interfaces() {
  std::set<std::string> interfaces;
  std::ifstream input("/proc/net/route");
  if (!input) {
    return interfaces;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (const auto interface = detail::parse_default_route_interface(line)) {
      interfaces.insert(*interface);
    }
  }
  return interfaces;
}

std::string format_network_row(const detail::NetworkInterfaceRow& row) {
  return "name=" + core::redact_likely_secret(row.name) +
         " oper_state=" + core::redact_likely_secret(row.oper_state) +
         " type=" + core::redact_likely_secret(row.type) +
         " mtu=" + core::redact_likely_secret(row.mtu) +
         " carrier=" + core::redact_likely_secret(row.carrier) +
         " mac=" + core::redact_likely_secret(row.mac_address) +
         " ipv4=" + core::redact_likely_secret(detail::format_network_address_list(row.ipv4_addresses)) +
         " ipv6=" + core::redact_likely_secret(detail::format_network_address_list(row.ipv6_addresses)) +
         " rx_bytes=" + std::to_string(row.rx_bytes) +
         " tx_bytes=" + std::to_string(row.tx_bytes) +
         " default_route=" + std::string(row.default_route ? "true" : "false") +
         " source=" + core::redact_likely_secret(row.source) + '\n';
}

}  // namespace

namespace detail {

std::optional<std::string> parse_default_route_interface(std::string_view line) {
  const auto fields = split_whitespace(line);
  if (fields.size() < 8 || fields[0] == "Iface") {
    return std::nullopt;
  }
  if (fields[0].empty() || fields[1] != "00000000" || fields[7] != "00000000") {
    return std::nullopt;
  }

  unsigned long flags = 0;
  try {
    flags = std::stoul(std::string(fields[3]), nullptr, 16);
  } catch (...) {
    return std::nullopt;
  }

  constexpr unsigned long kRouteUp = 0x1;
  if ((flags & kRouteUp) == 0) {
    return std::nullopt;
  }

  return std::string(fields[0]);
}

std::string format_network_address_list(const std::vector<std::string>& addresses) {
  if (addresses.empty()) {
    return "none";
  }

  std::string output;
  const auto count = std::min(addresses.size(), kMaxAddressesPerFamily);
  for (std::size_t index = 0; index < count; ++index) {
    if (index > 0) {
      output += ',';
    }
    output += addresses[index];
  }
  if (addresses.size() > kMaxAddressesPerFamily) {
    output += ",truncated";
  }
  return output;
}

std::string format_network_summary_body(const std::vector<NetworkInterfaceRow>& rows,
                                        bool has_more_rows) {
  std::string body;
  bool truncated = false;
  append_bounded(body, "network_interfaces_count=" + std::to_string(rows.size()) + '\n',
                 truncated);
  if (rows.empty()) {
    append_bounded(body, "no_network_interfaces=true\n", truncated);
  }

  for (const auto& row : rows) {
    append_bounded(body, format_network_row(row), truncated);
    if (truncated) {
      return body;
    }
  }

  if (has_more_rows) {
    append_bounded(body, "truncated=true\n", truncated);
  }
  return body;
}

}  // namespace detail

NetworkSummaryProvider default_network_summary_provider() {
  return [] {
    const std::filesystem::path net_root = "/sys/class/net";
    if (!std::filesystem::exists(net_root)) {
      return detail::NetworkSummaryResult{
          .ok = false,
          .error = "network interfaces are not available",
      };
    }

    const auto addresses = collect_interface_addresses();
    const auto default_routes = collect_default_route_interfaces();
    std::vector<detail::NetworkInterfaceRow> rows;
    bool has_more_rows = false;

    std::vector<std::string> interface_names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(net_root, error)) {
      interface_names.push_back(entry.path().filename().string());
    }
    if (error) {
      return detail::NetworkSummaryResult{
          .ok = false,
          .error = "network interfaces are not available",
      };
    }
    std::ranges::sort(interface_names);

    for (const auto& name : interface_names) {
      const auto interface_path = net_root / name;
      InterfaceAddresses row_addresses;
      if (const auto found = addresses.find(name); found != addresses.end()) {
        row_addresses = found->second;
      }

      if (rows.size() < kMaxNetworkRows) {
        rows.push_back(detail::NetworkInterfaceRow{
            .name = name,
            .oper_state = read_text_file(interface_path / "operstate"),
            .type = read_text_file(interface_path / "type"),
            .mtu = read_text_file(interface_path / "mtu"),
            .carrier = read_text_file(interface_path / "carrier"),
            .mac_address = read_text_file(interface_path / "address"),
            .ipv4_addresses = std::move(row_addresses.ipv4),
            .ipv6_addresses = std::move(row_addresses.ipv6),
            .rx_bytes = read_uint_file(interface_path / "statistics" / "rx_bytes"),
            .tx_bytes = read_uint_file(interface_path / "statistics" / "tx_bytes"),
            .default_route = default_routes.contains(name),
            .source = "sysfs",
        });
      } else {
        has_more_rows = true;
      }
    }

    return detail::NetworkSummaryResult{
        .ok = true,
        .error = "",
        .rows = std::move(rows),
        .has_more_rows = has_more_rows,
    };
  };
}

NetworkSummaryTool::NetworkSummaryTool(NetworkSummaryProvider provider)
    : provider_(std::move(provider)) {}

std::string NetworkSummaryTool::name() const {
  return "network.summary";
}

core::RiskClass NetworkSummaryTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse NetworkSummaryTool::call(const core::ToolRequest& request) const {
  auto result = provider_();
  if (!result.ok) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = result.error.empty() ? "network summary provider failed" : result.error,
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "network interfaces listed",
      .evidence = {core::Evidence{
          .id = request.id + ":network.summary",
          .source = "network.summary",
          .summary = "bounded local network interface summary",
          .body = detail::format_network_summary_body(result.rows, result.has_more_rows),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
