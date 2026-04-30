# RPM Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a local RPM package installable with DNF on a new Fedora machine.

**Architecture:** Add small runtime-default path helpers shared by `kasli` and
`kaslid`, install binaries and a systemd user unit through CMake, and generate an
RPM with CPack. Document the build/install/smoke/remove workflow.

**Tech Stack:** C++23, CMake install rules, CPack RPM, systemd user units, RPM
tooling.

---

### Task 1: Runtime Defaults

**Files:**
- Create: `include/kasli/app/default_paths.hpp`
- Create: `src/app/default_paths.cpp`
- Create: `tests/unit/default_paths_test.cpp`
- Modify: `src/kasli-cli/main.cpp`
- Modify: `src/kaslid/main.cpp`
- Modify: `CMakeLists.txt`

- [x] Add `default_socket_path()` and `default_audit_log_path()` declarations.
- [x] Add tests showing socket path uses `XDG_RUNTIME_DIR` and falls back to
  `kaslid.sock`.
- [x] Add tests showing audit path uses `XDG_STATE_HOME`, falls back to
  `$HOME/.local/state/kasli/audit.jsonl`, then `kasli-audit.jsonl`.
- [x] Run `cmake --build build-fedora --target kasli_default_paths_test` and
  verify it fails because the implementation does not exist yet.
- [x] Implement the helpers.
- [x] Use `default_socket_path()` in the CLI default `--socket` option.
- [x] Use `default_socket_path()` and `default_audit_log_path()` in the daemon
  option defaults.
- [x] Run `cmake --build build-fedora --target kasli_default_paths_test &&
  ./build-fedora/kasli_default_paths_test` and verify the tests pass.

### Task 2: Install Rules And User Service

**Files:**
- Create: `packaging/systemd/user/kaslid.service`
- Modify: `CMakeLists.txt`

- [x] Add `include(GNUInstallDirs)`.
- [x] Add install rules for `kasli` and `kaslid` into `${CMAKE_INSTALL_BINDIR}`.
- [x] Add a systemd user unit that runs `/usr/bin/kaslid`.
- [x] Install the user unit into `lib/systemd/user`.
- [x] Run `cmake --install build-fedora --prefix build-fedora/stage` and verify
  the staged tree contains `bin/kasli`, `bin/kaslid`, and
  `lib/systemd/user/kaslid.service`.

### Task 3: CPack RPM Metadata

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`

- [x] Add CPack RPM package metadata for package name `kasli-os`, version
  `0.1.0`, release `1`, prototype license metadata, summary, vendor, and
  contact.
- [x] Document how to build the RPM with `cpack -G RPM`.
- [x] Document how to install, start, smoke test, stop, and remove it on Fedora.
- [x] Run `cpack -G RPM --config build-fedora/CPackConfig.cmake` and verify an
  RPM is produced.
- [x] Run `rpm -qpl <rpm>` and verify it contains `/usr/bin/kasli`,
  `/usr/bin/kaslid`, and `/usr/lib/systemd/user/kaslid.service`.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Release
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
cpack -G RPM --config build-fedora/CPackConfig.cmake
git diff --check
```

- [x] Run a staged-install smoke with `build-fedora/stage/bin/kaslid` and
  `build-fedora/stage/bin/kasli` using default socket behavior.
- [x] Request code review and address any critical or important findings.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The plan covers runtime defaults, package install content, CPack metadata,
documentation, RPM artifact verification, staged installed-binary smoke, review,
and commit. There are no placeholders or ambiguous tasks.
