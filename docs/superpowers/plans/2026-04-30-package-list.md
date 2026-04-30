# Package List Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `packages.list`, a bounded read-only installed package inventory tool.

**Architecture:** Extend the existing package tool module with a parser,
formatter, injectable command runner, and production fixed-argv RPM backend.
Register the tool in `kaslid` and document the new tool in the repo status
files.

**Tech Stack:** C++23, Catch2, Unix `fork`/`execv`, fixed RPM query on RPM
systems.

---

### Task 1: Tests And Public Contract

**Files:**
- Modify: `include/kasli/tools/package_tool.hpp`
- Modify: `tests/unit/package_tool_test.cpp`

- [x] Add `PackageListRow`, `PackageCommandResult`, parser/formatter
  declarations, `PackageListRunner`, and `PackageListTool`.
- [x] Add Catch2 tests for metadata, parser behavior, bounded body formatting,
  injected successful command output, and injected command failure.
- [x] Run `cmake --build build-fedora --target kasli_package_tool_test` and
  verify the tests fail because the new symbols are not implemented.

### Task 2: Implementation

**Files:**
- Modify: `src/tools/package_tool.cpp`

- [x] Implement `parse_rpm_package_list_line`.
- [x] Implement `format_package_list_body`.
- [x] Implement a fixed-argv RPM command runner using `fork`/`execv`, timeout,
  and output cap.
- [x] Implement `PackageListTool`.
- [x] Run `cmake --build build-fedora --target kasli_package_tool_test &&
  ./build-fedora/kasli_package_tool_test` and verify the package tests pass.

### Task 3: Daemon Registration And Docs

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`
- Modify: `docs/research/fedora-server-test.md`

- [x] Register `PackageListTool` in `kaslid`.
- [x] Document `packages.list` as implemented.
- [x] Remove `packages.list` from the remaining planned read-only tool lists.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
git diff --check
```

- [x] Run live daemon smoke checks for `tools-list` and `packages.list`.
- [x] Stop the daemon.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The design and plan cover the parser, formatter, bounded execution, daemon
registration, documentation, and verification. There are no placeholders or
open-ended implementation steps.
