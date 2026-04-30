# Disk Usage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `disk.usage`, a bounded read-only mounted filesystem usage tool.

**Architecture:** Add a dedicated disk tool module with mountinfo parsing,
virtual-filesystem filtering, injectable provider output, and production
`std::filesystem::space()` collection. Register the tool in `kaslid` and update
project status docs.

**Tech Stack:** C++23, Catch2, Linux `/proc/self/mountinfo`,
`std::filesystem::space()`.

---

### Task 1: Tests And Public Contract

**Files:**
- Create: `include/kasli/tools/disk_tool.hpp`
- Create: `tests/unit/disk_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [x] Add `DiskMountInfo`, `DiskUsageRow`, `DiskUsageResult`,
  `DiskUsageProvider`, parser/formatter declarations, and `DiskUsageTool`.
- [x] Add Catch2 tests for metadata, mountinfo parsing, escaped path decoding,
  virtual filesystem filtering, body formatting, injected successful provider
  output, and provider error propagation.
- [x] Add the new source and test target to `CMakeLists.txt`.
- [x] Run `cmake --build build-fedora --target kasli_disk_tool_test` and verify
  it fails because `src/tools/disk_tool.cpp` does not exist yet.

### Task 2: Implementation

**Files:**
- Create: `src/tools/disk_tool.cpp`

- [x] Implement `parse_mountinfo_line`.
- [x] Implement virtual filesystem filtering.
- [x] Implement `format_disk_usage_body`.
- [x] Implement the default `/proc/self/mountinfo` plus
  `std::filesystem::space()` provider.
- [x] Implement `DiskUsageTool`.
- [x] Run `cmake --build build-fedora --target kasli_disk_tool_test &&
  ./build-fedora/kasli_disk_tool_test` and verify the disk tests pass.

### Task 3: Daemon Registration And Docs

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`
- Modify: `docs/research/fedora-server-test.md`

- [x] Register `DiskUsageTool` in `kaslid`.
- [x] Document `disk.usage` as implemented.
- [x] Remove `disk.usage` from remaining planned read-only tool lists.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
git diff --check
```

- [x] Run live daemon smoke checks for `tools-list` and `disk.usage`.
- [x] Stop the daemon.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The design and plan cover the parser, formatter, provider, daemon registration,
documentation, and verification. There are no placeholders or ambiguous tasks.
