# Disk Usage Design

## Goal

Add `disk.usage`, a read-only mounted filesystem usage tool for the v1 evidence
layer.

## Design

`DiskUsageTool` is a new tool module:

- `include/kasli/tools/disk_tool.hpp`
- `src/tools/disk_tool.cpp`
- `tests/unit/disk_tool_test.cpp`

The production provider reads `/proc/self/mountinfo` on Linux, filters obvious
virtual filesystems, and calls `std::filesystem::space()` for each candidate
mount point. It does not run shell commands and does not mutate the system.

Unit tests inject synthetic provider rows and parser inputs so the tests do not
depend on the host mount table.

## Evidence Contract

The tool name is `disk.usage` and its risk class is `read_only`.

Evidence source is `disk.usage`. The body starts with:

```text
disk_mounts_count=<shown-row-count>
```

If no usable mount rows are found, the body includes:

```text
no_disk_mounts=true
```

Rows use stable key/value text:

```text
mount_point=/ fs_type=btrfs source=/dev/nvme1n1p3 capacity_bytes=1000 free_bytes=500 available_bytes=400 used_percent=60
```

The evidence body is bounded to 64 KiB and rows are capped at 100. If more rows
exist than the cap, the body includes `truncated=true`.

## Error Handling

If `/proc/self/mountinfo` is unavailable, return an error response with no
evidence. If individual mount points cannot be measured, skip those rows. If the
provider succeeds but no usable rows remain, return an ok response with
`no_disk_mounts=true`.

## Testing

Tests cover metadata, mountinfo parsing, escaped mountpoint decoding, virtual
filesystem filtering, body formatting, injected successful provider rows, and
provider error propagation.
