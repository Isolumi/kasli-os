# Read-Only Prototype Notes

The first prototype implements a distro-neutral C++ read-only management layer.

Implemented trust boundaries:

- No unrestricted shell execution.
- Tool calls are typed.
- Tool risk is checked by the policy broker using trusted tool metadata.
- Only read-only tools are allowed.
- Audit events are written as JSONL with UTC-ish timestamps.
- Model requests receive curated evidence only.
- The Unix socket daemon uses newline-delimited JSON frames. The socket path is changed to owner-only mode (`0600`) immediately after bind, peers are checked for the same effective UID on macOS (`LOCAL_PEERCRED`) and Linux (`SO_PEERCRED`) where available, and request/response lines are capped at 1 MiB. Oversized or missing-newline frames are rejected.
- Tool audit records include bounded request params, policy decision, response status/message, and evidence IDs/sources/summaries/timestamps. Evidence bodies are not copied into audit details.
- Model audit records include bounded prompt text, evidence IDs/sources/count, `model.response` metadata for successful calls, and timestamped `model.skipped`/`model.error` records for failed paths.
- `journal.query` requires an explicit non-empty `unit` selector in fixture and live paths. Missing or empty units return an error instead of broad logs.
- `systemd.unit.status` is implemented for `KASLI_HAS_SYSTEMD` builds as a read-only DBus status/property query. Live systemd and journal paths are gated behind `KASLI_HAS_SYSTEMD`; macOS builds use fixture journal and can only exercise the systemd unavailable behavior.

Manual checks:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
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

Ollama-backed `ask` requires Ollama to be running locally with the `llama3.2` model available. If Ollama is not running or the model is unavailable, `ask` returns a JSON error response and audits `model.error`.

If Ollama is running with `llama3.2` available:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --ask 'What OS is this?' --ask-tool system.info
```

## Verification Results (2026-04-26)

Environment: macOS/Darwin arm64 (`Darwin Mac 25.3.0`), Debug build. CMake did not find PkgConfig, so libsystemd was not built and the live Linux systemd path was not exercised in this run.

Commands run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --tools-list
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool system.info
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool journal.query --param unit=ssh.service
git status --short
```

Observed outcome after the hardening pass: CMake configured, the build succeeded, and `ctest` passed 65/65 tests.

CLI smoke checks used the implemented Unix socket `--once` path. `--tools-list` returned `ok: true` with `system.info`, `systemd.units.list`, `systemd.unit.status`, and `journal.query`; `--call-tool system.info` returned `ok: true` with Darwin 25.3.0 arm64 evidence. `--call-tool journal.query --param unit=ssh.service` returned `ok: true` with bounded, redacted fixture journal evidence.

The CLI accepts repeatable `--param key=value` tool parameters. `journal.query` tests and any direct journal smoke checks must pass `--param unit=ssh.service` or another explicit unit selector. On macOS/non-systemd builds, `systemd.units.list` and `systemd.unit.status` are registered by the daemon and return unavailable errors instead of silently disappearing from the typed registry. Ollama live `ask` was skipped because this macOS verification did not confirm a local Ollama daemon with `llama3.2` available. Live Linux systemd checks were skipped because the verification ran on macOS without libsystemd; the unavailable systemd test coverage passed for `systemd.units.list` and `systemd.unit.status`.
