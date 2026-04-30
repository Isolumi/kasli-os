# Hardware Summary Design

## Goal

Add `hardware.summary`, a read-only hardware identity and capacity summary tool
for the v1 evidence layer.

## Design

`HardwareSummaryTool` is a new tool module:

- `include/kasli/tools/hardware_tool.hpp`
- `src/tools/hardware_tool.cpp`
- `tests/unit/hardware_tool_test.cpp`

The production provider reads only local kernel and sysfs/procfs interfaces. It
uses `uname` for architecture, `/proc/cpuinfo` for CPU model and processor
counts, `/proc/meminfo` for memory and swap totals, safe DMI fields from
`/sys/class/dmi/id`, and DRM card device metadata from
`/sys/class/drm/card*/device`. It does not read serial numbers, product UUIDs,
or asset tags. It does not run shell commands and does not mutate the system.

Unit tests inject provider rows and parser inputs so tests do not depend on the
host hardware.

## Evidence Contract

The tool name is `hardware.summary` and its risk class is `read_only`.

Evidence source is `hardware.summary`. The body starts with:

```text
hardware_summary=1
```

Scalar rows use stable key/value text:

```text
architecture=x86_64
cpu_vendor=AuthenticAMD
cpu_model=AMD Ryzen 9 9900X 12-Core Processor
cpu_logical_processors=24
cpu_physical_cores=12
memory_total_bytes=64880467968
memory_available_bytes=56800952320
swap_total_bytes=8589930496
system_vendor=Gigabyte Technology Co., Ltd.
product_name=X870 AORUS ELITE WIFI7
board_vendor=Gigabyte Technology Co., Ltd.
board_name=X870 AORUS ELITE WIFI7
bios_vendor=American Megatrends International, LLC.
bios_version=F3
chassis_type=3
gpu_devices_count=2
```

GPU rows use:

```text
gpu=name=card2 vendor_id=0x10de device_id=0x2b85 class=0x030000 driver=nvidia source=sysfs
```

The evidence body is bounded to 64 KiB and GPU rows are capped at 16. If more
GPU rows exist than the cap, the body includes `truncated=true`.

## Error Handling

Missing optional files produce `unknown` or `0` fields and keep the response
successful. GPU discovery failure returns scalar hardware evidence without GPU
rows. The provider returns an error response only if basic `uname` collection
fails, because architecture is the minimum hardware fact this tool promises.

## Testing

Tests cover metadata, CPU parsing, memory parsing, bounded body formatting,
injected successful provider output, and provider error propagation.
