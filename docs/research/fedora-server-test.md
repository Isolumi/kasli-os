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

Leave this running.

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
systemd.units.list
systemd.unit.status
journal.query
```

Read basic OS info:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool system.info
```

Expected:

- JSON response with `"ok": true`.
- Evidence includes Fedora/Linux OS and kernel details.

List systemd units:

```sh
./build-fedora/kasli --socket build-fedora/kaslid.sock --call-tool systemd.units.list
```

Expected:

- JSON response with `"ok": true`.
- Evidence body includes bounded unit rows.
- If there are more than 200 units, output includes `truncated=true`.

## 6. Pick A Real Service Unit

Find available service names:

```sh
systemctl list-units --type=service | head -30
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

## 9. Check Audit Logs

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

## 10. Journal Permission Notes

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

## 11. Same-UID Socket Note

The daemon checks peer credentials where supported. The CLI and daemon should
run as the same user.

If you start `kaslid` with `sudo`, then a normal-user `kasli` call may fail
because the socket rejects different UIDs. Prefer running both as the same
normal user.

## 12. Optional Ollama Test

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

## 13. Record Results

After testing, record:

- Fedora version.
- Kernel version.
- Whether CMake found `libsystemd`.
- Test count and result.
- Which unit names worked.
- Whether journal access required `systemd-journal` group membership.
- Any failed commands and exact JSON responses.

