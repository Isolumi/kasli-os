# Kasli OS Prototype

Kasli is an early prototype for an AI-native Linux management layer. It is not
a Linux distribution yet, and it is not "Linux with a chatbot preinstalled."
The current implementation is a local, read-only C++ daemon and CLI that expose
typed operating-system inspection tools through a constrained interface.

The goal is to let an AI assistant understand and explain the machine through
real OS interfaces while keeping the model untrusted, actions auditable, and
system access tightly scoped.

## Current Scope

This repository currently builds:

- `kaslid`: a local daemon that owns the system-inspection tool registry.
- `kasli`: a CLI client that talks to `kaslid` over a Unix domain socket.
- A typed read-only tool API.
- A read-only policy broker backed by trusted tool metadata.
- Append-only JSONL audit logging.
- Optional Ollama-backed answering over curated tool evidence.

Implemented tools:

- `system.info`: returns OS and kernel identity evidence.
- `disk.usage`: lists bounded mounted filesystem usage from Linux mount
  information and filesystem space data.
- `hardware.summary`: lists bounded CPU, memory, safe DMI, and GPU device
  summary evidence from local kernel interfaces.
- `network.summary`: lists bounded local network interface state, addresses,
  byte counters, and default-route flags from Linux sysfs and route data.
- `packages.recent_changes`: reads bounded recent package activity from local
  DNF/DNF5/YUM history logs.
- `packages.list`: lists bounded installed package inventory on RPM systems
  through a fixed read-only query.
- `journal.query`: returns bounded, redacted journal evidence for an explicit
  systemd unit selector.
- `systemd.units.list`: lists bounded systemd unit state on Linux builds with
  `libsystemd`.
- `systemd.unit.status`: reads bounded systemd unit status on Linux builds with
  `libsystemd`; for `.service` units it includes service failure fields such as
  `Result`, `ExecMainCode`, and `ExecMainStatus`.
- `services.failed`: lists bounded failed systemd `.service` units on Linux
  builds with `libsystemd`.
- `services.enabled`: lists bounded enabled systemd `.service` unit files on
  Linux builds with `libsystemd`.
- `service.diagnose`: aggregates `systemd.unit.status` and `journal.query` for
  one unit, returning a compact read-only diagnosis evidence record plus the
  underlying status and journal evidence.

The prototype does not yet perform package changes, service changes, config
edits, rollback operations, or privileged admin actions.

## Safety Model

The current prototype deliberately avoids arbitrary command execution.

Important boundaries:

- The daemon only exposes registered typed tools.
- The policy broker uses trusted tool metadata rather than caller-provided risk.
- Only read-only tools are allowed in this version.
- Tool results are bounded and secret-like strings are redacted before evidence
  reaches the model.
- Audit records are written as JSONL with timestamps, request parameters, policy
  decisions, response metadata, and evidence references.
- Evidence bodies are not copied into audit metadata.
- The Unix socket uses newline-delimited JSON frames, owner-only socket mode
  (`0600`), same-UID peer checks where supported, and a 1 MiB line cap.
- The model is never allowed to call tools directly. The daemon executes tools
  first, then sends curated evidence to the model provider.

## Repository Layout

```text
.
|-- CMakeLists.txt
|-- cmake/
|-- docs/
|-- include/kasli/
|   |-- audit/
|   |-- core/
|   |-- ipc/
|   |-- model/
|   |-- policy/
|   |-- session/
|   `-- tools/
|-- src/
|   |-- kaslid/
|   |-- kasli-cli/
|   |-- audit/
|   |-- core/
|   |-- ipc/
|   |-- model/
|   |-- policy/
|   |-- session/
|   `-- tools/
`-- tests/
```

## Requirements

Minimum build requirements:

- CMake 3.24 or newer.
- A C++23-capable compiler.
- Git, because dependencies are fetched through CMake `FetchContent`.

Fetched automatically by CMake:

- Catch2 for tests.
- nlohmann/json.
- CLI11.

Optional:

- `libcurl`, for Ollama HTTP support.
- `pkg-config` and `libsystemd` development headers, for live systemd and
  journal integration on Linux.
- Ollama with the `llama3.2` model, for local AI answering.

On macOS, the project builds and tests the daemon, CLI, policy, audit, socket,
model-boundary, fixture-journal, and non-systemd behavior. Live systemd behavior
must be tested on Linux.

## Build And Test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected result on the current prototype: all tests pass.

## Run The Daemon

Start the daemon in one terminal:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl
```

Then use the CLI from another terminal:

```sh
./build/kasli --socket build/kaslid.sock --tools-list
./build/kasli --socket build/kaslid.sock --call-tool system.info
./build/kasli --socket build/kaslid.sock --call-tool disk.usage
./build/kasli --socket build/kaslid.sock --call-tool hardware.summary
./build/kasli --socket build/kaslid.sock --call-tool network.summary
./build/kasli --socket build/kaslid.sock --call-tool packages.recent_changes
./build/kasli --socket build/kaslid.sock --call-tool packages.list
./build/kasli --socket build/kaslid.sock --call-tool journal.query --param unit=ssh.service
./build/kasli --socket build/kaslid.sock --call-tool services.failed
./build/kasli --socket build/kaslid.sock --call-tool services.enabled
./build/kasli --socket build/kaslid.sock --call-tool service.diagnose --param unit=ssh.service
```

Inspect audit records:

```sh
tail -n 20 build/dev-audit.jsonl
```

Stop the daemon with `Ctrl-C`.

## One-Shot Smoke Checks

For quick manual checks, run the daemon with `--once`. Each command starts the
daemon for a single request:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --tools-list

./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool system.info

./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool journal.query --param unit=ssh.service
```

## Optional Ollama Use

Start Ollama and make sure `llama3.2` is available:

```sh
ollama pull llama3.2
```

With `kaslid` running:

```sh
./build/kasli --socket build/kaslid.sock --ask "What OS is this?" --ask-tool system.info
```

If Ollama is not running or `llama3.2` is unavailable, the CLI returns a JSON
error and the daemon audits `model.error`.

## Device Testing Matrix

Good first test device:

- macOS laptop or desktop. This verifies build, tests, Unix socket hardening,
  CLI behavior, audit logs, fixture journal behavior, redaction, and optional
  Ollama calls. It does not verify live systemd behavior.

Best next test device:

- Linux VM with systemd. Fedora, Ubuntu, Debian, openSUSE, or NixOS are all
  reasonable. This is the safest place to validate live `systemd.units.list`,
  `systemd.unit.status`, `services.failed`, `services.enabled`,
  `packages.recent_changes`, and `journal.query`.

Later hardware test:

- Spare Linux laptop or desktop. Use this after VM validation. Run as a normal
  user first; journal visibility depends on local permissions and group
  membership.

For Linux systemd testing, install a compiler, CMake, `pkg-config`, and
`libsystemd` development headers, then rebuild from scratch.

A fuller Fedora checklist, including a Codex handoff prompt for testing on the
Fedora machine itself, is in
[`docs/research/fedora-server-test.md`](docs/research/fedora-server-test.md).

Examples:

```sh
# Fedora
sudo dnf install cmake gcc-c++ pkgconf-pkg-config systemd-devel

# Ubuntu/Debian
sudo apt install cmake g++ pkg-config libsystemd-dev

# openSUSE
sudo zypper install cmake gcc-c++ pkg-config systemd-devel
```

Then run:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
./build-linux/kaslid --socket build-linux/kaslid.sock --audit-log build-linux/audit.jsonl
```

In another terminal:

```sh
./build-linux/kasli --socket build-linux/kaslid.sock --tools-list
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool disk.usage
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool hardware.summary
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool network.summary
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool packages.recent_changes
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool packages.list
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool systemd.units.list
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool systemd.unit.status --param unit=ssh.service
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool services.failed
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool services.enabled
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool journal.query --param unit=ssh.service
./build-linux/kasli --socket build-linux/kaslid.sock --call-tool service.diagnose --param unit=ssh.service
```

If your distro uses `sshd.service` instead of `ssh.service`, use that unit name.

## Current Limitations

- No mutating actions are implemented.
- No package mutation is implemented yet.
- No rollback integration is implemented yet.
- No desktop UI is implemented yet.
- Live systemd paths require Linux with `libsystemd`; macOS only exercises
  unavailable-path behavior.
- Journal access may be permission-limited depending on the user and distro.
- The Ollama provider currently uses the local `llama3.2` model name.

## Intended Direction

The next stages are to add more typed read-only tools, then introduce a
permission broker for carefully reviewed low-risk and admin actions. Any future
mutating tool should require explicit policy, confirmation, audit logging, and a
rollback strategy before it is enabled.
