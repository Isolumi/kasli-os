# Read-Only Prototype Notes

The first prototype implements a distro-neutral C++ read-only management layer.

Implemented trust boundaries:

- No unrestricted shell execution.
- Tool calls are typed.
- Tool risk is checked by the policy broker using trusted tool metadata.
- Only read-only tools are allowed.
- Audit events are written as JSONL.
- Model requests receive curated evidence only.
- `ask` skips model invocation on denied/error tool calls and audits `model.skipped`/`model.error`.
- Live systemd path is gated behind `KASLI_HAS_SYSTEMD` and was statically inspected on macOS; macOS build uses fixture journal.

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
```

If Ollama is running:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --ask 'What OS is this?' --ask-tool system.info
```
