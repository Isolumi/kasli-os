# Package List Design

## Goal

Add `packages.list`, a read-only installed package inventory tool for the v1
evidence layer.

## Research Notes

The Fedora validation host has `/usr/bin/rpm` available, but RPM development
headers/pkg-config metadata are not installed in the current build environment.
The existing v1 design allows a fixed package-manager command when a native
library is not available, provided it uses a fixed executable, fixed argv shape,
no shell, no user-controlled flags, timeout, and output-size limits.

## Design

`PackageListTool` lives beside `PackageRecentChangesTool` in
`include/kasli/tools/package_tool.hpp` and `src/tools/package_tool.cpp`.

The production backend runs a fixed RPM query:

```text
/usr/bin/rpm -qa --qf '%{NAME}\t%{EPOCHNUM}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{INSTALLTIME}\n'
```

The daemon executes it with `fork`/`execv`, not a shell. The command output is
capped, stderr is discarded, and a timeout kills a stuck child. Unit tests inject
synthetic command output instead of touching the host package database.

## Evidence Contract

The tool name is `packages.list` and its risk class is `read_only`.

Evidence source is `packages.list`. The body starts with:

```text
packages_count=<shown-row-count>
```

If no packages are found, the body includes:

```text
no_packages=true
```

Rows use stable key/value text:

```text
name=<name> epoch=<epoch> version=<version> release=<release> arch=<arch> install_time=<unix-seconds> source=rpmdb
```

The evidence body is bounded to 64 KiB and the row list is capped. If more rows
exist than the cap, the body includes `truncated=true`.

## Error Handling

If no fixed RPM executable exists, return an error response with no evidence.
If the command fails, times out, or exceeds the output cap, return an error
response with no evidence. Malformed rows are skipped.

## Testing

Tests cover metadata, RPM line parsing, malformed-line skipping, bounded body
formatting, injected successful command output, injected command failure, and
live tool registration through the daemon smoke path.
