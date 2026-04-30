# Power Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `power.status`, a bounded read-only local power supply summary tool.

**Architecture:** Add a dedicated power tool module with bounded sysfs row
formatting, injectable provider output, and production `/sys/class/power_supply`
collection. Register the tool in `kaslid` and update project status docs.

**Tech Stack:** C++23, Catch2, Linux `/sys/class/power_supply`.

---

### Task 1: Tests And Public Contract

**Files:**
- Create: `include/kasli/tools/power_tool.hpp`
- Create: `tests/unit/power_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [x] Add `PowerSupplyRow`, `PowerStatusResult`, `PowerStatusProvider`,
  formatter declarations, and `PowerStatusTool`.
- [x] Add Catch2 tests for metadata, body formatting with battery and AC rows,
  empty body formatting, injected successful provider output, and provider error
  propagation.
- [x] Add the new source and test target to `CMakeLists.txt`.
- [x] Run `cmake --build build-fedora --target kasli_power_tool_test` and
  verify it fails because `src/tools/power_tool.cpp` does not exist yet.

### Task 2: Implementation

**Files:**
- Create: `src/tools/power_tool.cpp`

- [x] Implement bounded power status body formatting.
- [x] Implement the default `/sys/class/power_supply` provider.
- [x] Ensure the provider never reads serial numbers or unbounded directory
  depth.
- [x] Implement `PowerStatusTool`.
- [x] Run `cmake --build build-fedora --target kasli_power_tool_test &&
  ./build-fedora/kasli_power_tool_test` and verify the power tests pass.

### Task 3: Daemon Registration And Docs

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`
- Modify: `docs/research/fedora-server-test.md`

- [x] Register `PowerStatusTool` in `kaslid`.
- [x] Document `power.status` as implemented.
- [x] Remove `power.status` from remaining planned read-only tool lists.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
git diff --check
```

- [x] Run live daemon smoke checks for `tools-list` and `power.status`.
- [x] Stop the daemon.
- [x] Request code review and address any critical or important findings.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The design and plan cover formatter behavior, empty host behavior, provider
collection, sensitive-field avoidance, daemon registration, documentation,
verification, and review. There are no placeholders or ambiguous tasks.
