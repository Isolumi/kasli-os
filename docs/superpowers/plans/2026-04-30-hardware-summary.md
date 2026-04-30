# Hardware Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `hardware.summary`, a bounded read-only local hardware summary tool.

**Architecture:** Add a dedicated hardware tool module with CPU and memory
parsers, safe DMI scalar fields, bounded DRM GPU rows, injectable provider
output, and production `uname`, `/proc`, and `/sys` collection. Register the
tool in `kaslid` and update project status docs.

**Tech Stack:** C++23, Catch2, POSIX `uname`, Linux `/proc/cpuinfo`,
`/proc/meminfo`, `/sys/class/dmi/id`, and `/sys/class/drm`.

---

### Task 1: Tests And Public Contract

**Files:**
- Create: `include/kasli/tools/hardware_tool.hpp`
- Create: `tests/unit/hardware_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [x] Add `CpuSummary`, `MemorySummary`, `DmiSummary`, `GpuDeviceRow`,
  `HardwareSummary`, `HardwareSummaryResult`, `HardwareSummaryProvider`, parser
  and formatter declarations, and `HardwareSummaryTool`.
- [x] Add Catch2 tests for metadata, CPU parser output, memory parser output,
  bounded body formatting, injected successful provider output, and provider
  error propagation.
- [x] Add the new source and test target to `CMakeLists.txt`.
- [x] Run `cmake --build build-fedora --target kasli_hardware_tool_test` and
  verify it fails because `src/tools/hardware_tool.cpp` does not exist yet.

### Task 2: Implementation

**Files:**
- Create: `src/tools/hardware_tool.cpp`

- [x] Implement `parse_cpuinfo_summary`.
- [x] Implement `parse_meminfo_summary`.
- [x] Implement bounded hardware body formatting.
- [x] Implement the default `uname`, `/proc`, DMI, and DRM provider.
- [x] Implement `HardwareSummaryTool`.
- [x] Run `cmake --build build-fedora --target kasli_hardware_tool_test &&
  ./build-fedora/kasli_hardware_tool_test` and verify the hardware tests pass.

### Task 3: Daemon Registration And Docs

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`
- Modify: `docs/research/fedora-server-test.md`

- [x] Register `HardwareSummaryTool` in `kaslid`.
- [x] Document `hardware.summary` as implemented.
- [x] Remove `hardware.summary` from remaining planned read-only tool lists.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
git diff --check
```

- [x] Run live daemon smoke checks for `tools-list` and `hardware.summary`.
- [x] Stop the daemon.
- [x] Request code review and address any critical or important findings.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The design and plan cover parser behavior, formatter behavior, provider
collection, daemon registration, documentation, verification, and review. There
are no placeholders or ambiguous tasks.
