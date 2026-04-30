# RPM Packaging Design

## Goal

Produce the first shipped-product artifact: a downloadable Fedora RPM that can
be installed on a new machine with `dnf install ./kasli-os-...rpm`.

## First Package Scope

The first package is a local RPM artifact, not a hosted DNF repository. A user
should be able to copy the RPM to another Fedora machine, install it with DNF,
start the daemon as their user, and call tools with the installed CLI.

In scope:

- install `/usr/bin/kasli`
- install `/usr/bin/kaslid`
- install a systemd user unit at `/usr/lib/systemd/user/kaslid.service`
- package with CPack RPM from the existing CMake build
- document local RPM build, install, start, smoke test, stop, and remove steps

Out of scope for this package:

- hosted DNF repository metadata
- automatic service enablement on install
- privileged system service
- SELinux policy
- signed RPMs
- automatic upgrade testing

## Runtime Defaults

Installed usage should not require users to pass `--socket` for normal cases.
Both CLI and daemon will default to:

```text
$XDG_RUNTIME_DIR/kaslid.sock
```

If `XDG_RUNTIME_DIR` is unavailable, they fall back to the current development
path:

```text
kaslid.sock
```

The daemon audit log default will be:

```text
$XDG_STATE_HOME/kasli/audit.jsonl
```

If `XDG_STATE_HOME` is unavailable but `HOME` exists, it falls back to:

```text
$HOME/.local/state/kasli/audit.jsonl
```

If neither exists, it falls back to `kasli-audit.jsonl`.

## Systemd User Unit

The RPM installs `packaging/systemd/user/kaslid.service` as a systemd user unit.
The unit runs:

```text
/usr/bin/kaslid
```

It relies on the daemon defaults above for socket and audit paths. Users start
it manually for a smoke test with:

```sh
systemctl --user start kaslid
```

Manual start keeps the first package conservative and avoids starting a local
daemon unexpectedly during install. Users can run
`systemctl --user enable --now kaslid` when they want persistent user-session
autostart.

## CPack RPM

CMake will install the two executables and user unit. CPack will generate an
RPM named like:

```text
kasli-os-0.1.0-1.<arch>.rpm
```

The package metadata will clearly mark this as a prototype package. License
metadata is `LicenseRef-Kasli-Prototype` until the project chooses a formal
license.

## Verification

Local verification must include:

- full build and CTest
- `cpack -G RPM`
- `rpm -qpl <rpm>` shows `/usr/bin/kasli`, `/usr/bin/kaslid`, and the user unit
- `rpm -qp --requires <rpm>` prints dependency metadata
- installed-binary smoke through a staged install or local package install

Clean-machine verification should be recorded later by installing the RPM on a
fresh Fedora VM with:

```sh
sudo dnf install ./kasli-os-0.1.0-1.*.rpm
systemctl --user start kaslid
kasli --tools-list
kasli --call-tool system.info
systemctl --user stop kaslid
sudo dnf remove kasli-os
```
