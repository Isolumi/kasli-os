# Power Status Design

## Goal

Add `power.status`, a read-only power supply and battery status summary tool for
the v1 evidence layer.

## Design

`PowerStatusTool` is a new tool module:

- `include/kasli/tools/power_tool.hpp`
- `src/tools/power_tool.cpp`
- `tests/unit/power_tool_test.cpp`

The production provider reads `/sys/class/power_supply` and one level of
well-known per-device attributes. It does not read `serial_number`, UUID-like
fields, or arbitrary deep files. It does not run shell commands and does not
mutate the system.

On desktops and servers with no exposed power supplies, the tool returns a
successful empty evidence response with `no_power_supplies=true`.

## Evidence Contract

The tool name is `power.status` and its risk class is `read_only`.

Evidence source is `power.status`. The body starts with:

```text
power_supplies_count=<shown-row-count>
```

If no power supplies are found, the body includes:

```text
no_power_supplies=true
```

Rows use stable key/value text:

```text
power_supply=name=BAT0 type=Battery status=Discharging online=unknown capacity_percent=87 health=Good technology=Li-ion energy_now=41000000 energy_full=50000000 charge_now=unknown charge_full=unknown power_now=12000000 voltage_now=11500000 current_now=unknown cycle_count=42 manufacturer=Example model_name=ExamplePack source=sysfs
```

AC adapter rows use the same shape with missing battery-specific attributes set
to `unknown`.

The evidence body is bounded to 64 KiB and rows are capped at 16. If more rows
exist than the cap, the body includes `truncated=true`.

## Error Handling

If `/sys/class/power_supply` is unavailable, return a controlled error response
with no evidence. If a single device disappears while being read, keep available
fields and use `unknown` for missing attributes. Directory iteration errors
return the same controlled unavailable error.

## Testing

Tests cover metadata, row formatting, empty body formatting, bounded body
formatting, injected successful provider output, and provider error propagation.
