# Kasli Progress

Last updated: 2026-05-04

This file tracks what works in the current prototype and how to verify it.

## Current State

Kasli is currently a read-only C++ daemon and CLI prototype for an AI-native
Linux management layer.

It is not a Linux distribution yet. It does not mutate the system. It does not
run arbitrary shell commands.

The repository is at a 0.1-style checkpoint. The package version in
`CMakeLists.txt` is `0.1.2`, so a public release tag should probably be
`v0.1.2` unless the version is changed before tagging. A human-readable release
checkpoint is recorded in `docs/releases/0.1-checkpoint.md`, and resume context
is recorded in `.planning/phases/0.1-release-checkpoint/.continue-here.md`.

The current prototype proves these pieces:

- `kaslid` runs as a local daemon.
- `kasli` talks to `kaslid` over a Unix domain socket.
- The daemon owns a typed tool registry.
- The policy broker allows registered read-only tools and denies other risk
  classes.
- Tool calls are audited to append-only JSONL.
- Evidence is bounded before it reaches the model path.
- Likely secret strings are redacted in implemented evidence tools.
- Optional Ollama or LM Studio/OpenAI-compatible integration can answer from
  selected evidence when a local model server is running.
- A local Fedora RPM can be built with CPack and installed with DNF.

## Working Tools

Current registered tools:

- `system.info`
- `disk.usage`
- `hardware.summary`
- `network.summary`
- `power.status`
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

### `disk.usage`

Lists bounded mounted filesystem usage on Linux.

Current behavior:

- reads `/proc/self/mountinfo`
- filters obvious virtual filesystems such as `proc`, `sysfs`, `tmpfs`, and
  cgroup mounts
- measures candidate mounts with `std::filesystem::space()`
- returns `disk_mounts_count=<n>`
- returns `no_disk_mounts=true` when no usable mount rows are found
- stores at most 100 mounted filesystem rows
- marks `truncated=true` when more rows exist

Verified on Fedora Linux 43.

### `hardware.summary`

Lists bounded local hardware identity and capacity evidence.

Current behavior:

- reads architecture with `uname`
- reads CPU model, vendor, logical processor count, and physical core count
  from `/proc/cpuinfo`
- reads memory and swap totals from `/proc/meminfo`
- reads safe DMI identity fields from `/sys/class/dmi/id`
- avoids serial numbers, product UUIDs, and asset tags
- lists at most 16 DRM GPU device rows from `/sys/class/drm/card*/device`
- marks `truncated=true` when more GPU rows exist

Verified on Fedora Linux 43.

### `network.summary`

Lists bounded local network interface state on Linux.

Current behavior:

- reads `/sys/class/net`
- attaches IPv4 and IPv6 addresses from `getifaddrs`
- marks interfaces that own a default route from `/proc/net/route`
- returns `network_interfaces_count=<n>`
- returns `no_network_interfaces=true` when no interface rows are found
- stores at most 100 interface rows
- marks `truncated=true` when more rows exist

Verified on Fedora Linux 43.

### `power.status`

Lists bounded local power supply and battery status.

Current behavior:

- reads `/sys/class/power_supply`
- returns `power_supplies_count=<n>`
- returns `no_power_supplies=true` on desktops or servers with no exposed power
  supplies
- reads fixed well-known attributes such as `type`, `status`, `online`,
  `capacity`, energy/charge fields, health, technology, manufacturer, and model
- avoids serial numbers and arbitrary deep sysfs traversal
- stores at most 16 power supply rows
- marks `truncated=true` when more rows exist

Verified on Fedora Linux 43.

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

Fedora Linux 43 KDE checkout after seamless install and model setup:

```text
160/160 tests passed
```

Fresh daemon/CLI checks passed on Fedora Linux 43 for `system.info`,
`systemd.units.list`, `systemd.unit.status`, `journal.query`,
`services.failed`, `services.enabled`, `packages.recent_changes`,
`packages.list`, `disk.usage`, `hardware.summary`, `network.summary`,
`power.status`, `service.diagnose`, and audit log metadata.

Release-checkpoint verification on Fedora Linux 43 KDE:

```text
ctest --test-dir build-fedora --output-on-failure
100% tests passed, 0 tests failed out of 160
```

Installed runtime smoke checks:

```text
kasli --tools-list
ok=true with the full read-only tool registry

kasli --ask "what os is this?" --ask-tool system.info
ok=true; answered Fedora Linux 43 from system.info evidence
```

Official Ollama `0.23.0` with `gemma4` was verified working. Fedora's
distro-packaged `ollama-0.9.4-4.fc43` reported version `0.0.0` and failed to
load `gemma4`, so release docs and diagnostics should steer users toward the
official Ollama install when they want `gemma4`.

Fedora RPM packaging now installs:

```text
/usr/bin/kasli
/usr/bin/kaslid
/usr/lib/systemd/user/kaslid.service
```

Installed runtime defaults use `$XDG_RUNTIME_DIR/kaslid.sock` and
`/run/user/$UID/kaslid.sock`, then `kaslid.sock`, for the daemon socket.
Audit defaults use `$XDG_STATE_HOME/kasli/audit.jsonl`, then
`$HOME/.local/state/kasli/audit.jsonl`, then `kasli-audit.jsonl`.

The installed `kasli` CLI starts `kaslid.service` on demand when the default
socket is missing. Model provider settings are read at daemon startup from
`KASLI_MODEL_PROVIDER`, `KASLI_MODEL_ENDPOINT`, and `KASLI_MODEL_NAME`.

## How To Verify Locally

Build and run tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Build the RPM:

```sh
sudo dnf install git cmake gcc-c++ pkgconf-pkg-config systemd-devel libcurl-devel rpm-build
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Release
cmake --build build-fedora
cpack -G RPM --config build-fedora/CPackConfig.cmake
rpm -qpl build-fedora/kasli-os-0.1.2-1.*.rpm
```

Install and smoke test on Fedora:

```sh
sudo dnf install ./build-fedora/kasli-os-0.1.2-1.*.rpm
kasli --tools-list
kasli --call-tool system.info
systemctl --user stop kaslid
sudo dnf remove kasli-os
```

Optional Ollama smoke:

```sh
ollama pull gemma4
kasli --ask "What OS is this?" --ask-tool system.info
```

Optional LM Studio smoke:

```sh
curl http://127.0.0.1:1234/v1/models
systemctl --user edit kaslid
```

Use a drop-in like this, replacing `local-model` with the id returned by
`/v1/models`:

```ini
[Service]
Environment=KASLI_MODEL_PROVIDER=openai-compatible
Environment=KASLI_MODEL_ENDPOINT=http://127.0.0.1:1234/v1
Environment=KASLI_MODEL_NAME=local-model
```

```sh
systemctl --user daemon-reload
systemctl --user restart kaslid
kasli --ask "What OS is this?" --ask-tool system.info
```

Start the daemon:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl
```

In another terminal:

```sh
./build/kasli --socket build/kaslid.sock --tools-list
./build/kasli --socket build/kaslid.sock --call-tool system.info
./build/kasli --socket build/kaslid.sock --call-tool disk.usage
./build/kasli --socket build/kaslid.sock --call-tool hardware.summary
./build/kasli --socket build/kaslid.sock --call-tool network.summary
./build/kasli --socket build/kaslid.sock --call-tool power.status
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
- inspect user or detailed security state
- select tools automatically for arbitrary questions
- perform privileged admin actions
- provide a desktop UI
- package itself as a hosted DNF repository or distro/remix

These are intentional limits until the read-only evidence layer is reliable.

## Next Recommended Work

Next user-facing step:

- design and implement a natural `kasli ask` flow that can select safe
  read-only tools automatically for common questions
- add a `kasli doctor` setup diagnostic for daemon status, socket reachability,
  model provider configuration, model availability, and known bad Ollama builds
  such as version `0.0.0`
- improve common setup errors so users do not need to interpret raw JSON or
  Ollama HTTP 500 failures

Next architecture step after that:

- add a small SQLite state store for snapshots and "what changed since
  yesterday?" support

Mutation should wait until there is a permission broker, confirmation flow,
audit records, and rollback design.
