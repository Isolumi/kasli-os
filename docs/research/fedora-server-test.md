# Fedora Server Test Runbook

Use this to test the Kasli read-only prototype on a real Fedora Server machine.

The goal is to validate the Linux-only paths that macOS cannot exercise:

- `KASLI_HAS_SYSTEMD=1`
- live systemd D-Bus reads
- live journal reads through `libsystemd`
- real unit status evidence
- journal permission behavior

Run the daemon as a normal user first. Avoid `sudo` unless you are specifically
testing root behavior.

## Codex Handoff

If you want a Codex session running directly on the Fedora machine to test this
prototype, start Codex from the repository root and give it this prompt:

```text
Test the Kasli read-only Linux prototype on this Fedora machine using
docs/research/fedora-server-test.md. Build in build-fedora, run the full test
suite, start kaslid as my normal user, exercise the CLI tools against live
systemd and journal data, inspect the audit log, and report exact failures with
commands and JSON responses. Do not add mutating tools, do not run arbitrary
system-changing commands, and do not use sudo unless a documented test step
explicitly requires it.
```

Before starting Codex on Fedora, make sure the Fedora checkout has the latest
local source changes from the Mac. If you copied files manually instead of
pulling from Git, re-sync the repo first.

## 1. Install Dependencies

On Fedora Server:

```sh
sudo dnf install -y git cmake gcc-c++ pkgconf-pkg-config systemd-devel libcurl-devel
```

## 2. Copy Or Clone The Repo

If the repo is pushed to a remote:

```sh
git clone <your-repo-url> kasli_os
cd kasli_os
```

If copying from the Mac:

```sh
rsync -av --exclude build --exclude .git /Users/isolumi/Documents/CS/kasli_os/ user@fedora-server:~/kasli_os/
```

Then SSH into Fedora:

```sh
ssh user@fedora-server
cd ~/kasli_os
```

## 3. Build Fresh On Fedora

Use a separate build directory so Fedora output does not mix with macOS output:

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
```

Expected:

- CMake finds `pkg-config`.
- CMake finds `libsystemd`.
- Tests pass.

If CMake does not find `pkg-config` or `libsystemd`, re-check the dependency
install step.

## 4. Start The Daemon

In terminal 1 on Fedora:

```sh
./build-fedora/kaslid \
  --socket build-fedora/kaslid.sock \
  --audit-log build-fedora/audit.jsonl
```

Leave this running. It is normal for this command to look like it is hanging:
the daemon is waiting for CLI requests. Stop it with `Ctrl-C` when done.

## 5. Run CLI Smoke Tests

In terminal 2 on Fedora:

```sh
cd ~/kasli_os
```

List tools:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --tools-list
```

Expected tools:

```text
system.info
disk.usage
packages.recent_changes
packages.list
systemd.units.list
systemd.unit.status
services.failed
services.enabled
journal.query
service.diagnose
```

Read basic OS info:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool system.info
```

Expected:

- JSON response with `"ok": true`.
- Evidence includes Fedora/Linux OS and kernel details.

Read disk usage:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool disk.usage
```

Expected:

- JSON response with `"ok": true`.
- Evidence body includes `disk_mounts_count=<n>`.
- Root or other persistent mounted filesystems appear when measurable.

Read recent package changes:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool packages.recent_changes
```

Expected:

- JSON response with `"ok": true` when package history logs are readable.
- Evidence body includes `package_changes_count=<n>`.
- If readable logs exist but no changes are found, evidence includes
  `no_package_changes=true`.

Read installed package inventory:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool packages.list
```

Expected:

- JSON response with `"ok": true` when `rpm` is available.
- Evidence body includes `packages_count=<n>`.
- If there are more than 500 package rows, output includes `truncated=true`.

List systemd units:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool systemd.units.list
```

Expected:

- JSON response with `"ok": true`.
- Evidence body includes bounded unit rows.
- If there are more than 200 units, output includes `truncated=true`.

Test failed services:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool services.failed
```

Expected:

- JSON response with `"ok": true`.
- Evidence body includes `failed_services_count=<n>`.
- If there are no failed services, evidence includes `no_failed_services=true`.
- If failed services are present, rows contain service names to pass to
  `service.diagnose`.

Test enabled services:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool services.enabled
```

Expected:

- JSON response with `"ok": true`.
- Evidence body includes `enabled_services_count=<n>`.
- If there are no enabled services, evidence includes
  `no_enabled_services=true`.

## 6. Pick A Real Service Unit

Find available service names:

```sh
systemctl list-units --type=service --all | head -40
```

Common Fedora candidates:

```text
sshd.service
dbus-broker.service
systemd-journald.service
NetworkManager.service
```

If `sshd.service` is not present, choose a unit shown by `systemctl`.

## 7. Test Unit Status

Example with SSH:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock \
  --call-tool systemd.unit.status \
  --param unit=sshd.service
```

Expected:

- JSON response with `"ok": true`.
- Evidence includes fields such as:
  - `unit=sshd.service`
  - `load_state=...`
  - `active_state=...`
  - `sub_state=...`
  - `fragment_path=...`
- For `.service` units, evidence should also include:
  - `service_result=...`
  - `exec_main_code=...`
  - `exec_main_status=...`

## 8. Test Journal Query

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock \
  --call-tool journal.query \
  --param unit=sshd.service
```

Expected:

- JSON response with `"ok": true`, if your user can read journal entries.
- Evidence is bounded and scoped to the requested unit.
- Secret-like strings are redacted.

If the response is an error or empty, check journal permissions and whether the
unit has recent logs.

To confirm that a unit has recent logs before asking Kasli:

```sh
journalctl -u systemd-journald.service -n 5 --no-pager
```

## 9. Test Service Diagnosis

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock \
  --call-tool service.diagnose \
  --param unit=sshd.service
```

Expected:

- JSON response with `"ok": true` if either status or journal evidence can be
  collected.
- First evidence source is `service.diagnose`.
- Response also includes underlying `systemd.unit.status` and `journal.query`
  evidence when those dependency calls succeed.
- Diagnosis evidence includes fields such as:
  - `status_tool_status=...`
  - `journal_tool_status=...`
  - `active_state=...`
  - `journal_entries_present=...`
  - `diagnosis=...`

If `sshd.service` is not present, use a real unit from step 6.

## 10. Check Audit Logs

```sh
tail -n 20 build-fedora/audit.jsonl
```

Expected:

- `tool.call` records for each CLI request.
- Timestamps are present.
- `policy_decision` is `read-only tool allowed`.
- Request params are recorded.
- Evidence refs are recorded.
- Full evidence bodies are not copied into audit metadata.

## 11. Journal Permission Notes

Fedora may restrict system journal access for normal users.

Check current groups:

```sh
id
```

If needed, add your user to the journal group:

```sh
sudo usermod -aG systemd-journal "$USER"
```

Then log out and back in:

```sh
exit
ssh user@fedora-server
cd ~/kasli_os
```

Retry `journal.query`.

## 12. Same-UID Socket Note

The daemon checks peer credentials where supported. The CLI and daemon should
run as the same user.

If you start `kaslid` with `sudo`, then a normal-user `kasli` call may fail
because the socket rejects different UIDs. Prefer running both as the same
normal user.

## 13. Troubleshooting Current Prototype Issues

Do not split the value for `--call-tool` onto the next shell line unless the
previous line ends with `\`. This is wrong:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool
  system.info
```

Use one line:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool system.info
```

Or use a continued command:

```sh
./build-fedora/kasli \
  --socket build-fedora/kaslid.sock \
  --call-tool system.info
```

If `systemd.unit.status --param unit=sshd.service` fails, first verify that the
unit exists on that Fedora system:

```sh
systemctl list-units --type=service --all | rg 'ssh|journal|NetworkManager|dbus'
```

Then query a unit that appears in the list, for example:

```sh
./build-fedora/kasli \
  --socket build-fedora/kaslid.sock \
  --call-tool systemd.unit.status \
  --param unit=systemd-journald.service
```

If `journal.query` returns `"ok": true` with an empty evidence body, the tool
worked but found no readable matching entries for that unit. Try a unit that
`journalctl -u <unit> -n 5 --no-pager` shows entries for.

If the daemon repeatedly prints `accept failed: Resource temporarily unavailable`,
rebuild from the latest source. The listening socket should not use a receive
timeout.

If `systemd.units.list` returns `failed to finish reading systemd unit list`,
rebuild from the latest source. The tool must drain the full D-Bus array even
when it only stores the first 200 units.

## 14. Optional Ollama Test

If Ollama is installed on the Fedora server:

```sh
ollama pull llama3.2
```

With `kaslid` running:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock \
  --ask "What OS is this?" \
  --ask-tool system.info
```

Expected:

- The daemon first calls `system.info`.
- The model receives curated evidence.
- Audit log includes `tool.call`, `model.request`, and `model.response`.

If Ollama is unavailable, the CLI returns a JSON error and the audit log records
`model.error`.

## 15. Record Results

After testing, record:

- Fedora version.
- Kernel version.
- Whether CMake found `libsystemd`.
- Test count and result.
- Which unit names worked.
- Whether `disk.usage` returned bounded mounted-filesystem evidence.
- Whether `packages.recent_changes` returned bounded package-change evidence.
- Whether `packages.list` returned bounded installed-package evidence.
- Whether `services.failed` returned a bounded failed-service summary.
- Whether `services.enabled` returned a bounded enabled-service summary.
- Whether `service.diagnose` returned aggregate and underlying evidence.
- Whether journal access required `systemd-journal` group membership.
- Any failed commands and exact JSON responses.
