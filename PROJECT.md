# Kasli Project Overview

This document explains how the current Kasli prototype works, what problem it
is trying to solve, what is implemented today, and where the project should go
next.

## Product Idea

Kasli is an AI-native operating-system management layer.

The long-term goal is not to ship a Linux desktop with a chatbot app. The goal
is to build an OS-level assistant that understands the machine through real,
typed system interfaces:

- packages
- services
- logs
- hardware
- users and permissions
- configuration
- files
- desktop state
- installed applications
- rollback or snapshot state

The AI model is not trusted. It may reason over evidence and explain the
machine, but it must not receive raw privileged access or arbitrary shell
execution. System access belongs to the daemon, and the daemon only exposes
typed tools guarded by policy and audit logging.

## Current Stage

The project is currently a read-only C++ prototype.

It is not yet:

- a Linux distribution
- a bootable image
- a desktop UI
- a package manager
- a mutation engine
- a privileged repair agent

It is currently:

- a local daemon named `kaslid`
- a CLI client named `kasli`
- a typed read-only tool registry
- a read-only policy broker
- an append-only JSONL audit logger
- a bounded evidence collection layer
- an optional local model interface through Ollama

The prototype proves the first important boundary: the model can answer
questions using curated system evidence without getting unrestricted system
access.

## Design Principles

Kasli follows these principles:

1. Do not put AI logic in the Linux kernel.
2. Keep privileged system access inside a daemon.
3. Do not trust the model.
4. Use typed tools instead of unrestricted shell commands.
5. Prefer local-first inference.
6. Make every tool call auditable.
7. Keep evidence bounded and redact likely secrets.
8. Do not implement system mutation until the read-only layer is reliable.
9. When mutation is added later, require policy, confirmation, audit, and
   rollback.

## High-Level Architecture

```text
user
  |
  v
kasli CLI
  |
  | Unix domain socket, newline-delimited JSON
  v
kaslid daemon
  |
  +-- session service
  |     orchestrates tool calls, model requests, and audit events
  |
  +-- policy broker
  |     allows only trusted read-only tools in the current prototype
  |
  +-- typed tool registry
  |     owns system.info, journal.query, systemd.units.list,
  |     systemd.unit.status, services.failed, service.diagnose
  |
  +-- audit log
  |     append-only JSONL event stream
  |
  +-- model provider
        optional Ollama HTTP provider; receives curated evidence only
```

The daemon is the authority boundary. The CLI does not inspect the machine
directly. The model does not inspect the machine directly.

## Main Executables

### `kaslid`

`kaslid` is the daemon. It:

- creates the typed tool registry
- creates the policy broker from trusted tool metadata
- opens the audit log
- listens on a Unix domain socket
- accepts JSON requests from `kasli`
- executes allowed read-only tool calls
- optionally asks a local model using curated evidence
- writes audit records

### `kasli`

`kasli` is the CLI client. It:

- builds JSON requests
- sends them to `kaslid` over the Unix socket
- prints the JSON response

It currently supports:

```sh
./build/kasli --socket build/kaslid.sock --tools-list
./build/kasli --socket build/kaslid.sock --call-tool system.info
./build/kasli --socket build/kaslid.sock --call-tool journal.query --param unit=ssh.service
./build/kasli --socket build/kaslid.sock --call-tool services.failed
./build/kasli --socket build/kaslid.sock --call-tool service.diagnose --param unit=ssh.service
./build/kasli --socket build/kaslid.sock --ask "What OS is this?" --ask-tool system.info
```

## Current Tool Registry

### `system.info`

Reads basic OS and kernel identity.

On Linux, it can use `/etc/os-release`. On macOS, `os_release=unavailable` is
expected because `/etc/os-release` is a Linux convention. It still returns
Darwin kernel and architecture information.

### `journal.query`

Reads bounded journal evidence for a specific systemd unit.

The current contract requires:

```text
unit=<service-name>
```

The explicit unit selector is required so the tool does not return broad recent
logs. On macOS and non-systemd builds, this uses fixture data for testing. On
Linux with `libsystemd`, it uses the journal API.

### `systemd.units.list`

Lists bounded systemd unit state on Linux builds with `libsystemd`.

It caps the result set at 200 rows and marks truncated output. The D-Bus reader
drains the row it enters before applying the cap so bounded output can still
return successfully on machines with many units.

### `systemd.unit.status`

Reads bounded status for one unit on Linux builds with `libsystemd`.

The current contract requires:

```text
unit=<unit-name>
```

For `.service` units, it includes service failure evidence when available:

- `Result`
- `ExecMainCode`
- `ExecMainStatus`

Those fields are important for questions like "why did this service fail?"

### `services.failed`

Lists bounded failed systemd `.service` units on Linux builds with
`libsystemd`.

It returns a compact evidence body with `failed_services_count=<n>` and one row
per failed service up to the configured cap. When there are no failed services,
it returns `no_failed_services=true`. This gives the assistant a read-only way
to find candidate units before calling `service.diagnose`.

### `service.diagnose`

Aggregates service status and journal evidence for one systemd unit.

The current contract requires:

```text
unit=<unit-name>
```

It calls the typed `systemd.unit.status` and `journal.query` tools internally,
then returns a compact diagnosis evidence record containing:

- dependency tool statuses and messages
- key systemd state fields such as `active_state`, `sub_state`, and
  `service_result`
- whether journal entries were present
- whether journal text contained common failure terms
- a short diagnosis string such as `service is failed`

The response also keeps the underlying status and journal evidence records so
the model can reason from source evidence rather than only the derived summary.

## Request Flow

### Tool Call Flow

```text
kasli --call-tool system.info
  |
  v
JSON request:
  method = tool.call
  tool.name = system.info
  tool.risk = read_only
  params = {}
  |
  v
kaslid
  |
  +-- validates request shape
  +-- asks policy broker whether the tool is allowed
  +-- finds tool in registry
  +-- executes the tool
  +-- writes audit event
  +-- returns JSON response
```

### AI Ask Flow

```text
kasli --ask "What OS is this?" --ask-tool system.info
  |
  v
kaslid
  |
  +-- calls system.info through the same policy/tool/audit path
  +-- if the tool fails or is denied, skips the model
  +-- builds a compact evidence bundle
  +-- sends prompt + evidence to Ollama
  +-- audits model.request
  +-- audits model.response or model.error
  +-- returns the answer or error
```

The model never chooses arbitrary commands. It only sees evidence collected by
policy-approved tools.

## IPC Protocol

The CLI and daemon communicate over a Unix domain socket.

Transport properties:

- newline-delimited JSON frames
- one request per line
- one response per line
- 1 MiB line size cap
- socket file mode changed to `0600`
- same effective UID peer checks where supported:
  - `LOCAL_PEERCRED` on macOS
  - `SO_PEERCRED` on Linux

Malformed, oversized, or missing-newline frames are rejected.

## Policy Model

The current policy broker is intentionally simple:

- deny by default
- allow registered read-only tools
- deny unknown tools
- deny trusted tools whose risk class is not read-only
- ignore caller attempts to spoof the risk class

The important detail is that policy is built from trusted registry metadata.
The caller can include a `risk` field in the request, but the policy broker does
not trust that field as the source of truth.

Later versions should replace or extend this with a permission broker similar
in spirit to polkit.

## Audit Model

Audit logs are written as JSONL.

Each tool call records:

- event id
- timestamp
- actor
- event type
- tool name
- request id
- request risk
- bounded request params
- policy decision
- response status
- response message
- evidence count
- evidence references

Evidence references include IDs, sources, summaries, and timestamps. Full
evidence bodies are not copied into the audit metadata.

Model paths record:

- `model.request`
- `model.response`
- `model.error`
- `model.skipped`

This makes it clear when the model was invoked and when it was deliberately
skipped because a tool was denied or failed.

## Evidence Handling

Tool output is treated as evidence, not as instructions.

The current evidence rules are:

- keep output bounded
- redact common secret-like strings
- preserve enough source metadata for auditability
- send only curated evidence to the model
- avoid sending broad logs unless explicitly scoped by a typed selector

The current redactor handles common forms such as:

- `password=...`
- `password: ...`
- `token=...`
- `Authorization: Bearer ...`
- `api_key=...`

This is not a complete data-loss-prevention system. It is a first protective
layer. More robust secret detection is future work.

## Model Boundary

The model provider is behind an interface.

Current provider:

- Ollama over local HTTP
- default model name: `llama3.2`

The model receives:

- user prompt
- curated evidence records

The model does not receive:

- shell access
- file-system access
- package manager access
- service manager access
- raw journal access
- permission to call tools directly

If a tool call is denied or fails, `ask_with_tool` does not invoke the model.
It returns an error and audits `model.skipped`.

## Build System

The project uses CMake and C++23.

Important dependencies:

- Catch2 for tests
- nlohmann/json for JSON serialization
- CLI11 for CLI parsing
- libcurl for Ollama HTTP when available
- libsystemd for live Linux systemd and journal support when available

CMake fetches Catch2, nlohmann/json, and CLI11 through `FetchContent`.

`libsystemd` is optional. If `pkg-config` or `libsystemd` is not found, the
project still builds, and systemd tools return controlled unavailable responses
where appropriate.

## Test Strategy

The current test suite covers:

- core typed schema serialization
- timestamp format
- audit log append behavior and failure handling
- policy allow/deny behavior
- trusted risk metadata
- tool registry behavior
- system info fixture behavior
- journal filtering, redaction, truncation, and required unit selector
- non-systemd unavailable behavior
- systemd status evidence formatting
- bounded systemd unit-list formatting
- service diagnosis aggregation and partial-failure behavior
- session orchestration
- model request/response/error/skipped auditing
- Ollama prompt construction and response parsing
- JSON protocol envelopes
- Unix socket framing, permissions, stale socket handling, and size limits

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected current result:

```text
79/79 tests passed
```

## Supported Test Devices

### macOS

Good for:

- compiling the prototype
- running the full test suite
- testing daemon and CLI behavior
- testing socket hardening
- testing audit logs
- testing fixture-backed journal evidence
- testing optional Ollama calls

Not good for:

- live systemd testing
- live journal testing

### Linux VM With systemd

Best next validation target.

Good for:

- live `systemd.units.list`
- live `systemd.unit.status`
- live `services.failed`
- live `journal.query`
- journal permission behavior
- packaging assumptions

Recommended distros:

- Fedora
- Ubuntu
- Debian
- openSUSE
- NixOS

### Spare Linux Hardware

Useful after VM validation.

Good for:

- real hardware identity
- power and battery tools later
- GPU and driver detection later
- real-world journal and service state

## What Happens On macOS

On macOS, `system.info` returns Darwin data and reports
`os_release=unavailable`.

That is expected.

`journal.query` uses fixture data.

That is expected.

`systemd.units.list`, `systemd.unit.status`, and `services.failed` are visible
in the tool registry, but live systemd support is not built because macOS does
not have systemd.

That is expected.

## What Needs Linux Validation

The following paths should be validated on a Linux VM or Linux machine:

- `KASLI_HAS_SYSTEMD=1` build detection
- live systemd D-Bus connection
- `systemd.units.list`
- `systemd.unit.status --param unit=<unit>`
- `services.failed`
- `service.diagnose --param unit=<unit>`
- live `journal.query --param unit=<unit>`
- journal permission behavior for normal users
- service failure evidence on a deliberately failed service

## Current Limitations

No mutation exists yet.

The project cannot currently:

- install packages
- remove packages
- restart services
- enable or disable services
- edit config files
- create rollback snapshots
- restore rollback snapshots
- inspect desktop application state
- inspect user accounts deeply
- inspect hardware beyond basic system info
- answer arbitrary multi-tool diagnostic questions automatically

The current `--ask` mode uses one selected evidence tool. `service.diagnose`
is the first explicit aggregate evidence tool for a question like "why did
ssh.service fail?" A future orchestrator should select the right aggregate or
primitive tools automatically.

## What To Build Next

The recommended next stage is still read-only.

Add tools that make the assistant better at explaining the machine:

- `packages.recent_changes`
- `services.enabled`
- `packages.list`
- `hardware.summary`
- `power.status`
- `disk.usage`
- `network.summary`
- `users.summary`
- `security.baseline`

After those tools exist, add a state store:

- SQLite for observations
- observed state snapshots
- evidence metadata
- tool-call history
- "what changed since yesterday?" support

Only after read-only diagnostics are solid should the project add mutating
actions.

## Future Mutating Action Model

Future actions should be divided into risk classes:

- read-only queries
- low-risk user changes
- admin changes
- destructive or high-risk changes

Before enabling admin changes, Kasli needs:

- a real permission broker
- human confirmation UI
- typed action schemas
- preflight checks
- audit records
- rollback plans
- backend-specific safety rules

For the first mutating backend, NixOS is likely the safest target because
changes can be represented as declarative diffs and rolled back. Fedora Atomic
is likely a strong later desktop remix target because of rpm-ostree, Flatpak,
SELinux, polkit, and mainstream hardware support.

## Mental Model

Kasli should be thought of as an OS evidence and action broker.

The model is the reasoning component.

The daemon is the authority boundary.

Typed tools are the only way to inspect the machine.

Policy decides whether a tool can run.

Audit records what happened.

Rollback is required before future mutation becomes acceptable.

That separation is the core of the project.
