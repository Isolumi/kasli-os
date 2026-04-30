# Network Summary Design

## Goal

Add `network.summary`, a read-only local network interface summary tool for the
v1 evidence layer.

## Design

`NetworkSummaryTool` is a new tool module:

- `include/kasli/tools/network_tool.hpp`
- `src/tools/network_tool.cpp`
- `tests/unit/network_tool_test.cpp`

The production provider reads interface metadata and counters from
`/sys/class/net`, attaches numeric IPv4/IPv6 addresses using `getifaddrs`, and
marks default-route interfaces by parsing `/proc/net/route`. It does not run
shell commands and does not mutate the system.

Unit tests inject provider rows and parser inputs so tests do not depend on the
host network state.

## Evidence Contract

The tool name is `network.summary` and its risk class is `read_only`.

Evidence source is `network.summary`. The body starts with:

```text
network_interfaces_count=<shown-row-count>
```

If no interfaces are found, the body includes:

```text
no_network_interfaces=true
```

Rows use stable key/value text:

```text
name=wlp8s0 oper_state=up type=1 mtu=1500 carrier=1 mac=aa:bb:cc:dd:ee:ff ipv4=192.168.1.20 ipv6=none rx_bytes=123 tx_bytes=456 default_route=true source=sysfs
```

The evidence body is bounded to 64 KiB and rows are capped at 100. Address lists
are comma-separated and bounded per interface. If more interface rows exist than
the cap, the body includes `truncated=true`.

## Error Handling

If `/sys/class/net` is unavailable, return an error response with no evidence.
If optional files for a single interface are unavailable, report `unknown` or
`0` for that field and keep the row. If address discovery or route parsing
fails, return interface rows without that enrichment.

## Testing

Tests cover metadata, default-route parsing, address formatting, bounded body
formatting, injected successful provider rows, and provider error propagation.
