# Network Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `network.summary`, a bounded read-only local network interface summary tool.

**Architecture:** Add a dedicated network tool module with route parsing,
address formatting, injectable provider output, and production `/sys/class/net`
plus `getifaddrs` collection. Register the tool in `kaslid` and update project
status docs.

**Tech Stack:** C++23, Catch2, Linux `/sys/class/net`, `/proc/net/route`,
POSIX `getifaddrs`.

---

### Task 1: Tests And Public Contract

**Files:**
- Create: `include/kasli/tools/network_tool.hpp`
- Create: `tests/unit/network_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [x] Add `NetworkInterfaceRow`, `NetworkSummaryResult`,
  `NetworkSummaryProvider`, parser/formatter declarations, and
  `NetworkSummaryTool`.
- [x] Add Catch2 tests for metadata, default route parsing, address list
  formatting, body formatting, injected successful provider output, and provider
  error propagation.
- [x] Add the new source and test target to `CMakeLists.txt`.
- [x] Run `cmake --build build-fedora --target kasli_network_tool_test` and
  verify it fails because `src/tools/network_tool.cpp` does not exist yet.

### Task 2: Implementation

**Files:**
- Create: `src/tools/network_tool.cpp`

- [x] Implement `parse_default_route_interface`.
- [x] Implement bounded address-list formatting.
- [x] Implement `format_network_summary_body`.
- [x] Implement the default `/sys/class/net`, `getifaddrs`, and
  `/proc/net/route` provider.
- [x] Implement `NetworkSummaryTool`.
- [x] Run `cmake --build build-fedora --target kasli_network_tool_test &&
  ./build-fedora/kasli_network_tool_test` and verify the network tests pass.

### Task 3: Daemon Registration And Docs

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `PROJECT.md`
- Modify: `docs/research/fedora-server-test.md`

- [x] Register `NetworkSummaryTool` in `kaslid`.
- [x] Document `network.summary` as implemented.
- [x] Remove `network.summary` from remaining planned read-only tool lists.

### Task 4: Verification And Commit

**Commands:**

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
git diff --check
```

- [x] Run live daemon smoke checks for `tools-list` and `network.summary`.
- [x] Stop the daemon.
- [x] Commit the design, plan, implementation, tests, and docs.

## Self-Review

The design and plan cover the parser, formatter, provider, daemon registration,
documentation, and verification. There are no placeholders or ambiguous tasks.
