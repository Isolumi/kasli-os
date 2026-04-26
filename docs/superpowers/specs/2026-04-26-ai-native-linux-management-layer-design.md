# AI-Native Linux Management Layer Design

Date: 2026-04-26
Status: Design approved for documentation; implementation plan not yet written

## Goal

Build an AI-native Linux management layer, not a Linux desktop with a chatbot
preinstalled. The system should understand the machine through typed operating
system interfaces: services, logs, packages, hardware, users, permissions,
configuration, files, desktop state, and installed applications.

The first project is a distro-neutral, read-only C++ management daemon plus CLI.
The full distro/remix is intentionally deferred until the daemon boundary,
audit model, and read-only system understanding are solid.

The AI model is not trusted. It may reason over curated evidence and propose
actions, but it must not receive unrestricted system access or execute shell
commands in v1.

## Current Research Summary

Research checked on 2026-04-26.

### Existing AI-focused Linux or OS integrations

| Project | Verified current facts | Relevance | Source |
|---|---|---|---|
| Gnoppix AI Linux | Gnoppix describes AI as optional. Users can install AI core packages, run local open-source LLMs, or connect to closed providers. Its public material emphasizes model access, chat UI, RAG, plugins, and local/remote AI options. | Useful as an AI-focused Linux desktop example, but public docs do not show a typed, auditable OS management layer. | https://wiki.gnoppix.org/features/ai/ and https://wiki.gnoppix.org/community/gnoppix-ai/ |
| openKylin AI PC 2.0 | openKylin 2.0 added Kylin AI Assistant, an AI subsystem with SDKs for language/image/audio, fuzzy search, intelligent background features, AI model selection/configuration, and both local and cloud deployment modes. | Strong precedent for AI as an OS subsystem, but public information is mostly about UX and AI capability integration rather than safe admin actions. | https://www.openkylin.top/news/3479-en.html |
| deepin/UOS AI 2.0 | deepin describes UOS AI 2.0 as native AIOS work with system-level interaction, AI Search, AI FollowAlong, AI Taskbar, offline natural-language file/image search, and cloud/local model switching. | Strong desktop integration precedent. Does not establish the safety model required for privileged Linux management. | https://www.deepin.org/en/uos-ai-2-0-released/ |
| SUSE SLES 16 Agentic AI | SLES 16 includes `mcphost` as a technology preview. It uses MCP, starts with no permissions by default, and requires explicit permission configuration and a connected LLM. SUSE's product page positions this as groundwork for AI-assisted operations with human-in-the-loop approval. | Closest enterprise precedent for permission-bounded agentic Linux operations. Important validation for an MCP-compatible outer interface, but not a reason to make MCP the internal trust boundary. | https://documentation.suse.com/releasenotes/sles/html/releasenotes_sles_16.0/index.html and https://www.suse.com/products/server/ai/ |
| Ubuntu Inference Snaps | Canonical announced optimized inference snaps in public beta, with automatic selection of optimized engines, quantizations, and architectures for device silicon. Public beta examples include Intel and Ampere optimized DeepSeek R1 and Qwen 2.5 VL snaps. | Useful precedent for local model packaging and hardware-aware inference on Ubuntu. It is not an OS management AI system. | https://canonical.com/blog/canonical-releases-inference-snaps |
| RHEL AI | Red Hat Enterprise Linux AI combines Granite LLMs, InstructLab model alignment tools, and a bootable optimized RHEL image for individual server deployments and OpenShift AI. | AI platform distro, not an AI-native system administration layer. | https://access.redhat.com/products/red-hat-enterprise-linux-ai/ |

### Base distribution comparison

| Base | Rollback and safety | Package/customization model | Hardware and desktop support | Maintainability | Fit for this project |
|---|---|---|---|---|---|
| NixOS | Excellent. Nix and NixOS provide atomic upgrades, rollbacks, bootable old system configurations, and declarative system configuration. | Declarative Nix modules and flakes are ideal for machine-readable config diffs. Custom ISO images are possible through NixOS configuration. | Good but sometimes less straightforward for consumer/vendor hardware than Ubuntu/Fedora. | Strong reproducibility, steeper learning curve. | Best first mutating backend after the read-only core, because AI-generated changes can become explicit config diffs. Source: https://nixos.org/guides/how-nix-works/ |
| Fedora Silverblue/Kinoite | Strong. Fedora Atomic Desktops provide atomic updates and a reliability-focused desktop base. | rpm-ostree, Flatpak, Toolbox/Distrobox, SELinux, polkit, and systemd are mature. Custom remix path is more image/composition oriented than NixOS. | Very good mainstream Linux desktop support. | Strong long-term community and desktop ecosystem. | Best later polished desktop remix target. Sources: https://fedoraproject.org/atomic-desktops/kinoite/ |
| openSUSE MicroOS/Tumbleweed | Strong. MicroOS uses Btrfs snapshots, transactional updates, immutable root semantics, and reboot-to-snapshot rollback. Tumbleweed integrates Snapper/Btrfs rollback. | Standard RPMs and zypper. Snapshot state is concrete and inspectable. | Good, but desktop product direction is more split across MicroOS, Aeon, Kalpa, and Tumbleweed. | Strong rollback tooling; smaller mindshare than Fedora/Ubuntu. | Strong candidate for a snapshot/rollback backend. Sources: https://microos.opensuse.org/ and https://doc.opensuse.org/documentation/tumbleweed/snapper/ |
| Debian/Ubuntu custom live-build | Weak by default unless we add Btrfs/Snapper, Timeshift, systemd-sysext, or an atomic image strategy. | Excellent package ecosystem. Debian live-build is mature for custom live images. Ubuntu hardware/vendor support is excellent. | Excellent, especially Ubuntu. | Very maintainable, but mutable base makes safe AI actions harder. | Good for broad compatibility, not best first safety model. Source: https://live-team.pages.debian.net/live-manual/html/live-manual.en.html |
| Arch-based custom ISO | Weak by default. Rolling, mutable base. Rollback must be added. | archiso makes ISO creation straightforward. Packages are current. | Good for enthusiasts, less predictable for safety-focused product. | High maintenance. | Useful for experiments, not recommended as primary base. Source: https://man.archlinux.org/man/mkarchiso.1.en |

### Base OS recommendation

For v1, do not commit to a distro base. Build the daemon as a distro-neutral
management layer with read-only adapters.

For the first mutating backend after v1, prefer NixOS because it gives the
cleanest path from "AI proposes a change" to "human reviews a declarative diff"
to "system applies atomically" to "system rolls back." For a later consumer
desktop remix, Fedora Atomic Desktops are likely the best target because of
mainstream desktop support, rpm-ostree, Flatpak, SELinux, polkit, and hardware
compatibility.

## Proven, Speculative, Needs Prototyping

Proven:

- systemd exposes service manager state through a documented D-Bus API.
- polkit provides a standard authorization framework for privileged operations.
- NixOS, Fedora Atomic, and openSUSE MicroOS/Tumbleweed have real rollback or
  atomic update mechanisms.
- Local model runtimes such as Ollama and llama.cpp can be accessed through
  local APIs and used without cloud dependency.
- Existing AI OS work exists, but public examples mostly emphasize assistant,
  search, model packaging, or MCP hosting rather than a complete auditable,
  rollback-aware Linux management layer.

Speculative:

- A general-purpose local LLM can reliably choose the right diagnostic tools
  without explicit per-task workflows.
- Users will trust a permission broker UX for AI-proposed system changes.
- A single cross-distro action schema can cover package, service, config, and
  rollback operations cleanly without becoming too generic.

Needs prototyping:

- Quality of answers from compact system evidence bundles.
- Secret redaction accuracy on logs and config snippets.
- Best storage shape for snapshots, evidence, tool calls, and model-visible
  context.
- Whether an MCP-compatible outer interface should land in the MVP or wait
  until the internal API is stable.

## Architecture

```text
kasli-cli
  |
  v
kaslid daemon
  |
  +--> Session Orchestrator
  |     - receives user questions
  |     - classifies intent
  |     - chooses read-only tools
  |     - builds model context
  |     - asks the local model
  |     - returns answer with evidence
  |
  +--> Typed Tool Registry
  |     - system.info
  |     - systemd.units.list
  |     - systemd.unit.status
  |     - journal.query
  |     - packages.list
  |     - hardware.summary
  |     - power.status
  |
  +--> Policy Broker
  |     - v1: allow or deny read-only calls by schema and risk class
  |     - later: confirmation UI, polkit, admin auth, rollback preconditions
  |
  +--> Audit Logger
  |     - append-only JSONL events
  |     - records prompt, tool calls, evidence ids, response, proposed actions
  |
  +--> State Store
  |     - SQLite metadata and observed-state snapshots
  |     - optional full-text/vector index later
  |
  +--> Model Provider
        - Ollama HTTP API first
        - llama.cpp/OpenAI-compatible providers later
        - optional cloud fallback later
```

The daemon owns system access. The CLI is only a client. The model provider
receives curated context, not raw filesystem or shell access.

## Data Flow

```text
user prompt
  -> CLI sends request to daemon
  -> daemon records prompt event
  -> orchestrator classifies diagnostic intent
  -> orchestrator selects typed read-only tools
  -> policy broker validates each tool call
  -> tools collect structured evidence
  -> redactor removes likely secrets and marks untrusted text
  -> model provider receives compact context bundle
  -> model returns explanation and proposed next steps
  -> daemon records response and evidence ids
  -> CLI displays answer with cited local evidence
```

Example: `Why did ssh.service fail?`

The daemon calls `systemd.unit.status` and `journal.query` for `ssh.service`.
The model receives unit load/active/sub states, service result/exit status where
available, bounded journal excerpts, timestamps, and OS metadata. The model
does not receive arbitrary `/etc`, home directory files, or shell access.

## MVP Scope

The MVP should be genuinely OS-aware while remaining read-only.

In scope:

- `kaslid` system daemon.
- `kasli` CLI client.
- Typed tool registry.
- Read-only system info inspector.
- systemd unit listing and unit status through D-Bus.
- bounded journal querying through libsystemd.
- package inventory adapter with best-effort detection for common distros.
- hardware and power summary using stable interfaces where available.
- append-only JSONL audit log.
- SQLite state store for observed-state snapshots, evidence metadata, and tool
  call records.
- Ollama local model provider.
- Answers with local evidence references.

Out of scope for v1:

- arbitrary shell execution
- system mutation
- package installation/removal
- editing config files
- restarting/stopping/enabling services
- destructive file operations
- cloud model fallback unless the provider abstraction is already clean
- desktop UI
- ISO/remix build
- kernel changes

Target v1 questions:

- What changed since yesterday?
- Why did this service fail?
- Why is my battery draining?
- Explain this systemd error and propose a fix.
- What packages/services/hardware are relevant to Python, Rust, or GPU ML setup?
- What would hardening this laptop for travel involve?

For setup/hardening questions, v1 generates an evidence-backed plan only. It
does not apply changes.

## Safe Action Model

The action model is designed now but only read-only actions are implemented in
v1.

| Class | Examples | Permission | Confirmation UI | Logging | Rollback |
|---|---|---|---|---|---|
| Read-only queries | service status, journal query, package list, hardware summary | Allowed if policy grants the user/session that tool | No confirmation for normal bounded reads; visible disclosure in audit view | Full tool call, parameters, source ids, response | Not applicable |
| Low-risk user changes | changing app preference, adding user shell alias, creating a project config | User session permission | Explicit confirmation with diff | Before/after content hash, actor, reason, model context id | File backup or app-native undo required |
| Admin changes | install package, enable service, edit system config, firewall change | polkit/admin auth plus policy allow | Diff, impact summary, rollback plan, and auth prompt | Full preflight, exact backend operation, result, rollback handle | Required where backend supports it |
| Destructive/high-risk changes | delete data, wipe disk, disable security, remove user, relax firewall broadly | Blocked by default in MVP and early alpha | Later: strong confirmation, recovery plan, typed operation only | Full transcript and evidence bundle | Snapshot or verified backup required before action |

## Threat Model

Primary assets:

- root/admin authority
- user data and secrets
- logs and config files
- package and service state
- audit log integrity
- model prompts and responses

Main threats:

- prompt injection from logs, config files, package metadata, filenames, or web
  content
- secret leakage to local or cloud models
- model hallucination leading to unsafe action recommendations
- confused-deputy attacks through overbroad daemon tools
- arbitrary command execution disguised as diagnostics
- tampering with audit logs
- malicious packages or services producing misleading evidence
- cross-user privacy leaks on multi-user systems
- rollback assumptions that do not hold on a mutable base distro

Controls:

- no unrestricted shell in v1
- no filesystem crawling by default
- typed tools with explicit schemas, risk classes, and policy rules
- deny-by-default mutating operations
- bounded evidence windows for logs and files
- secret redaction before model context construction
- source taint labels for untrusted text
- append-only audit log with hash chaining later
- separate model provider from tool executor
- policy broker remains outside model control
- per-user/session access checks
- future admin actions require rollback preconditions

## Implementation Stack

Core:

- Language: C++23.
- Build: CMake.
- CLI: CLI11.
- D-Bus/systemd: sdbus-c++.
- Journal: libsystemd `sd-journal`.
- JSON: nlohmann/json initially; simdjson later if performance matters.
- Logging: spdlog.
- Formatting: fmt.
- HTTP/model API: libcurl or Boost.Beast.
- State: SQLite through raw sqlite3 or a thin C++ wrapper.
- Tests: Catch2 or GoogleTest.
- Quality gates: ASan, UBSan, clang-tidy, cppcheck, and fuzzing for parsers and
  policy decisions.

Model runtime:

- First: Ollama local HTTP API.
- Later: llama.cpp server or OpenAI-compatible local providers.
- Later optional: cloud fallback with explicit per-request disclosure and secret
  filtering.

Indexing/search:

- MVP: SQLite and bounded live queries.
- Month 3: SQLite FTS5 or Tantivy for local text search over indexed evidence.
- Later: LanceDB or Qdrant only if vector search proves useful.

UI:

- MVP: CLI first.
- Month 3: TUI or small desktop app.
- Later: GNOME extension, KDE plasmoid, Tauri, GTK, or Qt based on target distro
  and desktop.

OS integration sources:

- systemd D-Bus API: https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html
- polkit reference: https://polkit.pages.freedesktop.org/polkit/
- MCP specification: https://modelcontextprotocol.io/specification/draft
- Ollama API: https://docs.ollama.com/api
- llama.cpp server: https://github.com/ggerganov/llama.cpp

## Initial Repository Structure

This is the proposed implementation structure after the implementation plan is
approved.

```text
.
|-- CMakeLists.txt
|-- cmake/
|-- docs/
|   |-- research/
|   |-- superpowers/
|   |   `-- specs/
|-- include/
|   `-- kasli/
|       |-- audit/
|       |-- core/
|       |-- model/
|       |-- policy/
|       `-- tools/
|-- src/
|   |-- kaslid/
|   |-- kasli-cli/
|   |-- audit/
|   |-- core/
|   |-- model/
|   |-- policy/
|   `-- tools/
|       |-- hardware/
|       |-- journal/
|       |-- packages/
|       |-- power/
|       `-- systemd/
|-- tests/
|   |-- fixtures/
|   |-- unit/
|   `-- integration/
|-- packaging/
|   |-- systemd/
|   |-- debian/
|   |-- fedora/
|   |-- nix/
|   |-- opensuse/
|   `-- arch/
`-- tools/
    `-- dev/
```

## First Prototype Plan

Prototype goal: a daemon that can inspect system state read-only, expose a typed
tool registry, answer CLI questions through a local model, and record an audit
log. It must not execute arbitrary shell commands.

Planned commands after scaffolding:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/kasli-cli/kasli ask "why did ssh.service fail?"
./build/src/kasli-cli/kasli tools list
./build/src/kasli-cli/kasli audit tail
```

Prototype milestones:

1. Define core schemas:
   - `ToolRequest`
   - `ToolResponse`
   - `Evidence`
   - `AuditEvent`
   - `RiskClass`
   - `PolicyDecision`
2. Implement audit JSONL writer.
3. Implement policy broker with read-only allowlist.
4. Implement system info and systemd unit list/status tools.
5. Implement bounded journal query.
6. Implement CLI commands: `ask`, `tools list`, `audit tail`.
7. Implement Ollama provider and context builder.
8. Add tests for policy denial, audit serialization, redaction, and tool schema
   validation.

## Roadmap

### Week 1 prototype

- C++ project skeleton.
- `kaslid` can run in foreground dev mode.
- `kasli` CLI can list tools and call read-only tools.
- Audit log records prompts and tool calls.
- system info, systemd unit list/status, and bounded journal query work on at
  least one Linux test machine.
- Ollama provider can answer one service-failure question from curated evidence.

### Month 1 MVP

- Stable typed tool registry.
- SQLite evidence metadata store.
- Package inventory adapters for at least two distro families.
- Hardware and power summary.
- Better redaction and prompt-injection isolation.
- User-facing answers include evidence references.
- Tests cover tool schemas, policy rules, redaction, journal fixtures, and model
  context construction.
- No mutating operations.

### Month 3 usable alpha

- Optional MCP-compatible read-only adapter.
- TUI or minimal desktop UI for audit review and question answering.
- Snapshot/index history for "what changed since yesterday?"
- NixOS read-only adapter for generations and declarative config inspection.
- Fedora/openSUSE read-only rollback state adapters where feasible.
- Experimental action planner that produces diffs/plans but still does not
  execute by default.

### Long-term distro/remix packaging

- First mutating backend: NixOS, because it maps AI proposals to declarative
  config diffs and rollback generations.
- First polished desktop remix target: Fedora Atomic Desktop, because it offers
  mainstream desktop support, rpm-ostree, Flatpak, SELinux, polkit, and a strong
  atomic update story.
- openSUSE snapshot backend for Btrfs/Snapper environments.
- Debian/Ubuntu packages for broad compatibility.
- ISO/remix only after the daemon, policy model, and rollback behavior have
  survived real use.

## Implementation Defaults

- Test framework: Catch2 for the first prototype because it is lightweight and
  straightforward for small C++ components.
- CLI-to-daemon transport: Unix domain socket for v1. A D-Bus service facade can
  be added later for desktop integration, but v1 should keep the private API
  simple.
- System integration: D-Bus is used to talk to systemd and other OS services,
  not as the initial public daemon API.
- Package adapters: prefer native package databases and libraries where stable.
  If a package manager command is needed in v1, it must be a fixed executable
  plus fixed argv shape, with no shell, no user-controlled flags, a timeout, and
  output-size limits.
- MCP support: defer to Month 3 as a read-only outer adapter after the internal
  typed API stabilizes.
- UI: CLI first, then TUI for alpha. A desktop UI is deferred; if a standalone
  desktop app is needed, Qt is the default candidate because C++ integration is
  direct, while GNOME/KDE shell integrations can remain separate adapters.

## Acceptance Criteria For The First Implementation Plan

- The plan preserves the v1 no-mutation rule.
- The daemon has no unrestricted command execution path.
- Every tool has a typed schema, risk class, and policy rule.
- Every user question and tool call is auditable.
- Model-visible context is curated and bounded.
- Tests cover at least policy, audit, tool schema validation, redaction, and one
  service-failure diagnostic fixture.
- The implementation can run locally without cloud services.
