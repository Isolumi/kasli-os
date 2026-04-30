# Kasli Progress

Last updated: 2026-04-30

This file tracks what works in the current prototype and how to verify it.

## Current State

Kasli is currently a read-only C++ daemon and CLI prototype for an AI-native
Linux management layer.

It is not a Linux distribution yet. It does not mutate the system. It does not
run arbitrary shell commands.

The current prototype proves these pieces:

- `kaslid` runs as a local daemon.
- `kasli` talks to `kaslid` over a Unix domain socket.
- The daemon owns a typed tool registry.
- The policy broker allows registered read-only tools and denies other risk
  classes.
- Tool calls are audited to append-only JSONL.
- Evidence is bounded before it reaches the model path.
- Likely secret strings are redacted in implemented evidence tools.
- Optional Ollama integration can answer from selected evidence when a local
  Ollama server is running.

## Working Tools

Current registered tools:

- `system.info`
- `packages.recent_changes`
- `packages.list`
- `systemd.units.list`
- `systemd.unit.status`
- `services.failed`
- `services.enabled`
- `journal.query`
- `service.diagnose`

### `system.info`

Returns basic OS and kernel identity.

Verified on:

- macOS, with `os_release=unavailable` expected
- Fedora Linux 43 KDE, with live `/etc/os-release` and kernel evidence

### `packages.recent_changes`

Reads bounded recent package activity from local DNF/DNF5/YUM history logs.

Current behavior:

- parses package install, upgrade, downgrade, remove, and reinstall rows
- ignores RPM callback stop rows and scriptlets
- returns `package_changes_count=<n>`
- returns `no_package_changes=true` when readable logs exist but no package
  changes are found
- stores at most 100 recent package-change rows
- marks `truncated=true` when more rows exist

Verified on Fedora Linux 43.

### `packages.list`

Lists bounded installed package inventory on RPM systems.

Current behavior:

- runs a fixed `rpm -qa --qf` query with no shell and no caller-controlled flags
- enforces command timeout and output-size limits
- returns `packages_count=<n>`
- returns `no_packages=true` when the query succeeds but no valid package rows
  are found
- stores at most 500 installed package rows
- marks `truncated=true` when more rows exist

Verified on Fedora Linux 43.

### `systemd.units.list`

Lists bounded live systemd units on Linux builds with `libsystemd`.

Current behavior:

- stores at most 200 unit rows
- marks `truncated=true` when more rows exist
- drains the full D-Bus array so large hosts do not fail while closing the
  response

Verified on Fedora Linux 43.

### `systemd.unit.status`

Reads bounded status for one systemd unit.

For `.service` units it includes:

- `service_result`
- `exec_main_code`
- `exec_main_status`

Verified on Fedora Linux 43 with `systemd-journald.service`.

### `services.failed`

Lists failed systemd `.service` units on Linux builds with `libsystemd`.

Current behavior:

- returns `failed_services_count=<n>`
- returns `no_failed_services=true` when no failed services are found
- stores at most 100 failed service rows
- marks `truncated=true` when more failed services exist
- drains the full D-Bus array so large hosts do not fail while closing the
  response

Verified on Fedora Linux 43.

### `services.enabled`

Lists enabled systemd `.service` unit files on Linux builds with `libsystemd`.

Current behavior:

- returns `enabled_services_count=<n>`
- returns `no_enabled_services=true` when no enabled services are found
- includes `enabled` and `enabled-runtime` service states
- stores at most 200 enabled service rows
- marks `truncated=true` when more enabled services exist
- drains the full D-Bus array so large hosts do not fail while closing the
  response

Verified on Fedora Linux 43.

### `journal.query`

Reads bounded journal entries for an explicit unit selector.

Current behavior:

- requires `unit=<unit-name>`
- returns at most 50 entries
- caps evidence body size
- redacts common secret-like strings

Verified on Fedora Linux 43 as a normal user.

### `service.diagnose`

Aggregates `systemd.unit.status` and `journal.query` for one unit.

Current behavior:

- requires `unit=<unit-name>`
- returns a compact `service.diagnose` evidence record
- preserves underlying `systemd.unit.status` and `journal.query` evidence
- reports partial success if one dependency works and the other fails
- fails only when both dependencies fail

This is the first MVP step toward answering "why did this service fail?" using
typed OS evidence instead of shell access.

## Verified Test Results

Previous local macOS checkout before `services.failed`:

```text
74/74 tests passed
```

The macOS checkout should be retested after pulling the `services.failed`
commit.

Fedora Linux 43 KDE checkout after `packages.list`:

```text
99/99 tests passed
```

Fresh daemon/CLI checks passed on Fedora Linux 43 for `system.info`,
`systemd.units.list`, `systemd.unit.status`, `journal.query`,
`services.failed`, `services.enabled`, `packages.recent_changes`,
`packages.list`, `service.diagnose`, and audit log metadata.

## How To Verify Locally

Build and run tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Start the daemon:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl
```

In another terminal:

```sh
./build/kasli --socket build/kaslid.sock --tools-list
./build/kasli --socket build/kaslid.sock --call-tool system.info
./build/kasli --socket build/kaslid.sock --call-tool packages.recent_changes
./build/kasli --socket build/kaslid.sock --call-tool packages.list
./build/kasli --socket build/kaslid.sock --call-tool services.failed
./build/kasli --socket build/kaslid.sock --call-tool services.enabled
./build/kasli --socket build/kaslid.sock --call-tool service.diagnose --param unit=ssh.service
tail -n 20 build/dev-audit.jsonl
```

On Fedora, use a real unit from:

```sh
systemctl list-units --type=service --all | head -40
```

Example:

```sh
./build-fedora/kasli \
  --socket build-fedora/kaslid.sock \
  --call-tool service.diagnose \
  --param unit=systemd-journald.service
```

## Known Limitations

Kasli still cannot:

- install, remove, or update packages
- restart, enable, or disable services
- edit config files
- create or restore rollback snapshots
- inspect detailed hardware, battery, GPU, disk, network, or user state
- select tools automatically for arbitrary questions
- perform privileged admin actions
- provide a desktop UI
- package itself as a distro/remix

These are intentional limits until the read-only evidence layer is reliable.

## Next Recommended Work

Near-term read-only tools:

- `hardware.summary`
- `disk.usage`
- `network.summary`
- `power.status`

Next architecture step:

- add a small SQLite state store for snapshots and "what changed since
  yesterday?" support

Mutation should wait until there is a permission broker, confirmation flow,
audit records, and rollback design.
