# Read-Only Core Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first C++ prototype of the Kasli read-only Linux management layer: typed tools, policy checks, audit logging, daemon/CLI plumbing, local model calls, and read-only system evidence.

**Architecture:** The prototype uses a C++23 core library shared by `kaslid` and `kasli`. The model is never allowed to execute tools directly; requests flow through a typed registry, a read-only policy broker, and an append-only audit logger. Linux system integrations are isolated behind tool adapters so distro-specific backends can be added without changing the trust boundary.

**Tech Stack:** C++23, CMake, Catch2, nlohmann/json, CLI11, Unix domain sockets, libsystemd when available, libcurl for Ollama HTTP.

---

## File Structure

Create this structure during implementation:

```text
.
|-- CMakeLists.txt
|-- cmake/
|   `-- KasliDependencies.cmake
|-- include/
|   `-- kasli/
|       |-- audit/
|       |   `-- audit_log.hpp
|       |-- core/
|       |   |-- json.hpp
|       |   |-- types.hpp
|       |   `-- uuid.hpp
|       |-- ipc/
|       |   |-- line_protocol.hpp
|       |   `-- unix_socket.hpp
|       |-- model/
|       |   |-- model_provider.hpp
|       |   `-- ollama_provider.hpp
|       |-- policy/
|       |   `-- policy_broker.hpp
|       |-- session/
|       |   `-- session_service.hpp
|       `-- tools/
|           |-- journal_tool.hpp
|           |-- system_info_tool.hpp
|           |-- systemd_tool.hpp
|           |-- tool.hpp
|           `-- tool_registry.hpp
|-- src/
|   |-- audit/
|   |   `-- audit_log.cpp
|   |-- core/
|   |   |-- json.cpp
|   |   |-- types.cpp
|   |   `-- uuid.cpp
|   |-- ipc/
|   |   |-- line_protocol.cpp
|   |   `-- unix_socket.cpp
|   |-- kasli-cli/
|   |   `-- main.cpp
|   |-- kaslid/
|   |   `-- main.cpp
|   |-- model/
|   |   `-- ollama_provider.cpp
|   |-- policy/
|   |   `-- policy_broker.cpp
|   |-- session/
|   |   `-- session_service.cpp
|   `-- tools/
|       |-- journal_tool.cpp
|       |-- package_tool.cpp
|       |-- system_info_tool.cpp
|       |-- systemd_tool.cpp
|       `-- tool_registry.cpp
|-- tests/
|   |-- fixtures/
|   |   |-- journal/
|   |   |   `-- ssh_failed.jsonl
|   |   `-- os-release
|   |-- integration/
|   |   `-- unix_socket_test.cpp
|   |-- test_support/
|   |   |-- fake_model_provider.hpp
|   |   `-- temp_dir.hpp
|   `-- unit/
|       |-- audit_log_test.cpp
|       |-- core_types_test.cpp
|       |-- line_protocol_test.cpp
|       |-- model_context_test.cpp
|       |-- policy_broker_test.cpp
|       |-- session_service_test.cpp
|       |-- system_info_tool_test.cpp
|       `-- tool_registry_test.cpp
```

Responsibility boundaries:

- `core`: plain data types, JSON conversion, IDs, and small value helpers.
- `audit`: append-only JSONL event writer and reader helpers.
- `policy`: deny-by-default decisions based on tool risk and allowlist.
- `tools`: read-only OS adapters exposed through typed requests.
- `session`: orchestration for prompts, tool calls, model context, and auditing.
- `ipc`: Unix socket line-delimited JSON protocol.
- `model`: local model provider interface and Ollama implementation.
- `kaslid`: daemon executable.
- `kasli-cli`: CLI executable.

## Task 1: CMake Skeleton And Smoke Test

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/KasliDependencies.cmake`
- Create: `tests/unit/smoke_test.cpp`

- [ ] **Step 1: Write the failing smoke test**

Create `tests/unit/smoke_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("kasli test harness is wired") {
  REQUIRE(2 + 2 == 4);
}
```

- [ ] **Step 2: Add the initial CMake configuration**

Create `cmake/KasliDependencies.cmake`:

```cmake
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)

FetchContent_MakeAvailable(Catch2 nlohmann_json CLI11)
```

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)

project(kasli_os LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KASLI_ENABLE_SYSTEMD "Build systemd and journal adapters when libsystemd is available" ON)
option(KASLI_ENABLE_CURL "Build Ollama provider when libcurl is available" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KasliDependencies)

include(CTest)
include(Catch)

add_executable(kasli_smoke_test tests/unit/smoke_test.cpp)
target_link_libraries(kasli_smoke_test PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(kasli_smoke_test)
```

- [ ] **Step 3: Run the smoke test**

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: the build succeeds and CTest reports one passing test named `kasli test harness is wired`.

- [ ] **Step 4: Commit**

```sh
git add CMakeLists.txt cmake/KasliDependencies.cmake tests/unit/smoke_test.cpp
git commit -m "build: add C++ test skeleton"
```

## Task 2: Core Typed Schemas

**Files:**
- Create: `include/kasli/core/types.hpp`
- Create: `src/core/types.cpp`
- Create: `include/kasli/core/json.hpp`
- Create: `src/core/json.cpp`
- Create: `tests/unit/core_types_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing schema tests**

Create `tests/unit/core_types_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/core/types.hpp>

#include <nlohmann/json.hpp>

using kasli::core::Evidence;
using kasli::core::RiskClass;
using kasli::core::ToolRequest;
using kasli::core::ToolStatus;

TEST_CASE("risk class converts to and from strings") {
  REQUIRE(kasli::core::to_string(RiskClass::ReadOnly) == "read_only");
  REQUIRE(kasli::core::risk_class_from_string("read_only") == RiskClass::ReadOnly);
  REQUIRE(kasli::core::risk_class_from_string("admin") == RiskClass::Admin);
}

TEST_CASE("tool request serializes with typed risk") {
  ToolRequest request;
  request.id = "req-1";
  request.tool_name = "system.info";
  request.risk = RiskClass::ReadOnly;
  request.params = {{"format", "summary"}};

  nlohmann::json encoded = request;
  REQUIRE(encoded.at("id") == "req-1");
  REQUIRE(encoded.at("tool_name") == "system.info");
  REQUIRE(encoded.at("risk") == "read_only");
  REQUIRE(encoded.at("params").at("format") == "summary");

  ToolRequest decoded = encoded.get<ToolRequest>();
  REQUIRE(decoded.id == request.id);
  REQUIRE(decoded.tool_name == request.tool_name);
  REQUIRE(decoded.risk == request.risk);
  REQUIRE(decoded.params.at("format") == "summary");
}

TEST_CASE("tool response carries evidence records") {
  kasli::core::ToolResponse response;
  response.request_id = "req-1";
  response.status = ToolStatus::Ok;
  response.message = "system inspected";
  response.evidence.push_back(Evidence{
      .id = "ev-1",
      .source = "system.info",
      .summary = "Linux host",
      .body = "kernel=6.8",
      .timestamp = "2026-04-26T00:00:00Z",
  });

  nlohmann::json encoded = response;
  REQUIRE(encoded.at("status") == "ok");
  REQUIRE(encoded.at("evidence").at(0).at("id") == "ev-1");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R core_types
```

Expected: build fails because `kasli/core/types.hpp` does not exist.

- [ ] **Step 3: Add core types**

Create `include/kasli/core/types.hpp`:

```cpp
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kasli::core {

enum class RiskClass {
  ReadOnly,
  LowRiskUser,
  Admin,
  Destructive,
};

enum class ToolStatus {
  Ok,
  Denied,
  Error,
};

struct Evidence {
  std::string id;
  std::string source;
  std::string summary;
  std::string body;
  std::string timestamp;
};

struct ToolRequest {
  std::string id;
  std::string tool_name;
  RiskClass risk = RiskClass::ReadOnly;
  std::map<std::string, std::string> params;
};

struct ToolResponse {
  std::string request_id;
  ToolStatus status = ToolStatus::Error;
  std::string message;
  std::vector<Evidence> evidence;
};

struct AuditEvent {
  std::string id;
  std::string timestamp;
  std::string actor;
  std::string type;
  std::string summary;
  std::map<std::string, std::string> details;
};

std::string to_string(RiskClass risk);
RiskClass risk_class_from_string(const std::string& value);
std::string to_string(ToolStatus status);
ToolStatus tool_status_from_string(const std::string& value);

void to_json(nlohmann::json& json, const Evidence& evidence);
void from_json(const nlohmann::json& json, Evidence& evidence);
void to_json(nlohmann::json& json, const ToolRequest& request);
void from_json(const nlohmann::json& json, ToolRequest& request);
void to_json(nlohmann::json& json, const ToolResponse& response);
void from_json(const nlohmann::json& json, ToolResponse& response);
void to_json(nlohmann::json& json, const AuditEvent& event);
void from_json(const nlohmann::json& json, AuditEvent& event);

}  // namespace kasli::core
```

Create `src/core/types.cpp`:

```cpp
#include <kasli/core/types.hpp>

#include <stdexcept>

namespace kasli::core {

std::string to_string(RiskClass risk) {
  switch (risk) {
    case RiskClass::ReadOnly:
      return "read_only";
    case RiskClass::LowRiskUser:
      return "low_risk_user";
    case RiskClass::Admin:
      return "admin";
    case RiskClass::Destructive:
      return "destructive";
  }
  throw std::invalid_argument("unknown risk class");
}

RiskClass risk_class_from_string(const std::string& value) {
  if (value == "read_only") return RiskClass::ReadOnly;
  if (value == "low_risk_user") return RiskClass::LowRiskUser;
  if (value == "admin") return RiskClass::Admin;
  if (value == "destructive") return RiskClass::Destructive;
  throw std::invalid_argument("unknown risk class: " + value);
}

std::string to_string(ToolStatus status) {
  switch (status) {
    case ToolStatus::Ok:
      return "ok";
    case ToolStatus::Denied:
      return "denied";
    case ToolStatus::Error:
      return "error";
  }
  throw std::invalid_argument("unknown tool status");
}

ToolStatus tool_status_from_string(const std::string& value) {
  if (value == "ok") return ToolStatus::Ok;
  if (value == "denied") return ToolStatus::Denied;
  if (value == "error") return ToolStatus::Error;
  throw std::invalid_argument("unknown tool status: " + value);
}

void to_json(nlohmann::json& json, const Evidence& evidence) {
  json = nlohmann::json{
      {"id", evidence.id},
      {"source", evidence.source},
      {"summary", evidence.summary},
      {"body", evidence.body},
      {"timestamp", evidence.timestamp},
  };
}

void from_json(const nlohmann::json& json, Evidence& evidence) {
  evidence.id = json.at("id").get<std::string>();
  evidence.source = json.at("source").get<std::string>();
  evidence.summary = json.at("summary").get<std::string>();
  evidence.body = json.at("body").get<std::string>();
  evidence.timestamp = json.at("timestamp").get<std::string>();
}

void to_json(nlohmann::json& json, const ToolRequest& request) {
  json = nlohmann::json{
      {"id", request.id},
      {"tool_name", request.tool_name},
      {"risk", to_string(request.risk)},
      {"params", request.params},
  };
}

void from_json(const nlohmann::json& json, ToolRequest& request) {
  request.id = json.at("id").get<std::string>();
  request.tool_name = json.at("tool_name").get<std::string>();
  request.risk = risk_class_from_string(json.at("risk").get<std::string>());
  request.params = json.value("params", std::map<std::string, std::string>{});
}

void to_json(nlohmann::json& json, const ToolResponse& response) {
  json = nlohmann::json{
      {"request_id", response.request_id},
      {"status", to_string(response.status)},
      {"message", response.message},
      {"evidence", response.evidence},
  };
}

void from_json(const nlohmann::json& json, ToolResponse& response) {
  response.request_id = json.at("request_id").get<std::string>();
  response.status = tool_status_from_string(json.at("status").get<std::string>());
  response.message = json.at("message").get<std::string>();
  response.evidence = json.value("evidence", std::vector<Evidence>{});
}

void to_json(nlohmann::json& json, const AuditEvent& event) {
  json = nlohmann::json{
      {"id", event.id},
      {"timestamp", event.timestamp},
      {"actor", event.actor},
      {"type", event.type},
      {"summary", event.summary},
      {"details", event.details},
  };
}

void from_json(const nlohmann::json& json, AuditEvent& event) {
  event.id = json.at("id").get<std::string>();
  event.timestamp = json.at("timestamp").get<std::string>();
  event.actor = json.at("actor").get<std::string>();
  event.type = json.at("type").get<std::string>();
  event.summary = json.at("summary").get<std::string>();
  event.details = json.value("details", std::map<std::string, std::string>{});
}

}  // namespace kasli::core
```

Create `include/kasli/core/json.hpp`:

```cpp
#pragma once

#include <string>

namespace kasli::core {

std::string redact_likely_secret(const std::string& input);

}  // namespace kasli::core
```

Create `src/core/json.cpp`:

```cpp
#include <kasli/core/json.hpp>

#include <regex>

namespace kasli::core {

std::string redact_likely_secret(const std::string& input) {
  static const std::regex assignment_pattern(
      R"((password|passwd|token|secret|api[_-]?key)=([^ \n\r\t]+))",
      std::regex_constants::icase);
  return std::regex_replace(input, assignment_pattern, "$1=[REDACTED]");
}

}  // namespace kasli::core
```

- [ ] **Step 4: Wire the core library in CMake**

Replace `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.24)

project(kasli_os LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KASLI_ENABLE_SYSTEMD "Build systemd and journal adapters when libsystemd is available" ON)
option(KASLI_ENABLE_CURL "Build Ollama provider when libcurl is available" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KasliDependencies)

include(CTest)
include(Catch)

add_library(kasli_core
  src/core/json.cpp
  src/core/types.cpp
)
target_include_directories(kasli_core PUBLIC include)
target_link_libraries(kasli_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(kasli_smoke_test tests/unit/smoke_test.cpp)
target_link_libraries(kasli_smoke_test PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(kasli_smoke_test)

add_executable(kasli_core_types_test tests/unit/core_types_test.cpp)
target_link_libraries(kasli_core_types_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_core_types_test)
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: smoke and core type tests pass.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/kasli/core src/core tests/unit/core_types_test.cpp
git commit -m "feat: add typed core schemas"
```

## Task 3: Append-Only Audit Log

**Files:**
- Create: `include/kasli/audit/audit_log.hpp`
- Create: `src/audit/audit_log.cpp`
- Create: `tests/test_support/temp_dir.hpp`
- Create: `tests/unit/audit_log_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing audit tests**

Create `tests/test_support/temp_dir.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace kasli::test_support {

inline std::filesystem::path temp_path(const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / ("kasli_" + name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

}  // namespace kasli::test_support
```

Create `tests/unit/audit_log_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>

#include "../test_support/temp_dir.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

TEST_CASE("audit log appends one JSON event per line") {
  const auto dir = kasli::test_support::temp_path("audit_log_test");
  const auto path = dir / "audit.jsonl";

  kasli::audit::AuditLog log(path);
  log.append(kasli::core::AuditEvent{
      .id = "event-1",
      .timestamp = "2026-04-26T00:00:00Z",
      .actor = "tester",
      .type = "tool.call",
      .summary = "called system.info",
      .details = {{"tool", "system.info"}},
  });

  std::ifstream input(path);
  std::string line;
  REQUIRE(std::getline(input, line));

  auto json = nlohmann::json::parse(line);
  REQUIRE(json.at("id") == "event-1");
  REQUIRE(json.at("details").at("tool") == "system.info");
  REQUIRE_FALSE(std::getline(input, line));
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R audit
```

Expected: build fails because `kasli/audit/audit_log.hpp` does not exist.

- [ ] **Step 3: Implement the audit log**

Create `include/kasli/audit/audit_log.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <kasli/core/types.hpp>

namespace kasli::audit {

class AuditLog {
 public:
  explicit AuditLog(std::filesystem::path path);

  void append(const core::AuditEvent& event) const;
  const std::filesystem::path& path() const noexcept;

 private:
  std::filesystem::path path_;
};

}  // namespace kasli::audit
```

Create `src/audit/audit_log.cpp`:

```cpp
#include <kasli/audit/audit_log.hpp>

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace kasli::audit {

AuditLog::AuditLog(std::filesystem::path path) : path_(std::move(path)) {
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path());
  }
}

void AuditLog::append(const core::AuditEvent& event) const {
  std::ofstream output(path_, std::ios::app);
  if (!output) {
    throw std::runtime_error("failed to open audit log: " + path_.string());
  }
  const nlohmann::json encoded = event;
  output << encoded.dump() << '\n';
}

const std::filesystem::path& AuditLog::path() const noexcept {
  return path_;
}

}  // namespace kasli::audit
```

- [ ] **Step 4: Wire the audit library in CMake**

Modify the `kasli_core` target in `CMakeLists.txt`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
)
```

Add the audit test target below the core type test:

```cmake
add_executable(kasli_audit_log_test tests/unit/audit_log_test.cpp)
target_link_libraries(kasli_audit_log_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_audit_log_test)
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: audit, smoke, and core type tests pass.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/kasli/audit src/audit tests/test_support tests/unit/audit_log_test.cpp
git commit -m "feat: add append-only audit log"
```

## Task 4: Read-Only Policy Broker

**Files:**
- Create: `include/kasli/policy/policy_broker.hpp`
- Create: `src/policy/policy_broker.cpp`
- Create: `tests/unit/policy_broker_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing policy tests**

Create `tests/unit/policy_broker_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/policy/policy_broker.hpp>

using kasli::core::RiskClass;
using kasli::core::ToolRequest;

TEST_CASE("policy allows registered read-only tool") {
  kasli::policy::PolicyBroker broker({"system.info", "journal.query"});

  auto decision = broker.decide(ToolRequest{
      .id = "req-1",
      .tool_name = "system.info",
      .risk = RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE(decision.allowed);
  REQUIRE(decision.reason == "read-only tool allowed");
}

TEST_CASE("policy denies unregistered tool") {
  kasli::policy::PolicyBroker broker({"system.info"});

  auto decision = broker.decide(ToolRequest{
      .id = "req-2",
      .tool_name = "shell.exec",
      .risk = RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE_FALSE(decision.allowed);
  REQUIRE(decision.reason == "tool is not registered in policy allowlist");
}

TEST_CASE("policy denies mutating risk classes in v1") {
  kasli::policy::PolicyBroker broker({"packages.install"});

  auto decision = broker.decide(ToolRequest{
      .id = "req-3",
      .tool_name = "packages.install",
      .risk = RiskClass::Admin,
      .params = {},
  });

  REQUIRE_FALSE(decision.allowed);
  REQUIRE(decision.reason == "only read-only tools are allowed in v1");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R policy
```

Expected: build fails because `kasli/policy/policy_broker.hpp` does not exist.

- [ ] **Step 3: Implement the broker**

Create `include/kasli/policy/policy_broker.hpp`:

```cpp
#pragma once

#include <kasli/core/types.hpp>

#include <set>
#include <string>
#include <vector>

namespace kasli::policy {

struct PolicyDecision {
  bool allowed = false;
  std::string reason;
};

class PolicyBroker {
 public:
  explicit PolicyBroker(std::vector<std::string> allowed_tools);

  PolicyDecision decide(const core::ToolRequest& request) const;

 private:
  std::set<std::string> allowed_tools_;
};

}  // namespace kasli::policy
```

Create `src/policy/policy_broker.cpp`:

```cpp
#include <kasli/policy/policy_broker.hpp>

namespace kasli::policy {

PolicyBroker::PolicyBroker(std::vector<std::string> allowed_tools)
    : allowed_tools_(allowed_tools.begin(), allowed_tools.end()) {}

PolicyDecision PolicyBroker::decide(const core::ToolRequest& request) const {
  if (!allowed_tools_.contains(request.tool_name)) {
    return PolicyDecision{.allowed = false, .reason = "tool is not registered in policy allowlist"};
  }
  if (request.risk != core::RiskClass::ReadOnly) {
    return PolicyDecision{.allowed = false, .reason = "only read-only tools are allowed in v1"};
  }
  return PolicyDecision{.allowed = true, .reason = "read-only tool allowed"};
}

}  // namespace kasli::policy
```

- [ ] **Step 4: Wire the policy code in CMake**

Modify the `kasli_core` target:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/policy/policy_broker.cpp
)
```

Add the policy test:

```cmake
add_executable(kasli_policy_broker_test tests/unit/policy_broker_test.cpp)
target_link_libraries(kasli_policy_broker_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_policy_broker_test)
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all current tests pass.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/kasli/policy src/policy tests/unit/policy_broker_test.cpp
git commit -m "feat: add read-only policy broker"
```

## Task 5: Tool Interface, Registry, And System Info Tool

**Files:**
- Create: `include/kasli/tools/tool.hpp`
- Create: `include/kasli/tools/tool_registry.hpp`
- Create: `include/kasli/tools/system_info_tool.hpp`
- Create: `src/tools/tool_registry.cpp`
- Create: `src/tools/system_info_tool.cpp`
- Create: `tests/fixtures/os-release`
- Create: `tests/unit/tool_registry_test.cpp`
- Create: `tests/unit/system_info_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing registry and system info tests**

Create `tests/fixtures/os-release`:

```text
NAME="Kasli Test Linux"
VERSION_ID="1.0"
ID=kasli-test
PRETTY_NAME="Kasli Test Linux 1.0"
```

Create `tests/unit/tool_registry_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

TEST_CASE("tool registry lists registered tools") {
  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  auto names = registry.names();
  REQUIRE(names.size() == 1);
  REQUIRE(names.at(0) == "system.info");
  REQUIRE(registry.find("system.info") != nullptr);
  REQUIRE(registry.find("shell.exec") == nullptr);
}
```

Create `tests/unit/system_info_tool_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/system_info_tool.hpp>

TEST_CASE("system info tool returns os-release evidence") {
  kasli::tools::SystemInfoTool tool("tests/fixtures/os-release");
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.request_id == "req-1");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).source == "system.info");
  REQUIRE(response.evidence.at(0).body.find("Kasli Test Linux") != std::string::npos);
}
```

- [ ] **Step 2: Run the failing tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R "tool_registry|system_info"
```

Expected: build fails because the tool headers do not exist.

- [ ] **Step 3: Add the tool interface and registry**

Create `include/kasli/tools/tool.hpp`:

```cpp
#pragma once

#include <kasli/core/types.hpp>

#include <string>

namespace kasli::tools {

class Tool {
 public:
  virtual ~Tool() = default;

  virtual std::string name() const = 0;
  virtual core::RiskClass risk() const = 0;
  virtual core::ToolResponse call(const core::ToolRequest& request) const = 0;
};

}  // namespace kasli::tools
```

Create `include/kasli/tools/tool_registry.hpp`:

```cpp
#pragma once

#include <kasli/tools/tool.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kasli::tools {

class ToolRegistry {
 public:
  void add(std::unique_ptr<Tool> tool);
  const Tool* find(const std::string& name) const;
  std::vector<std::string> names() const;

 private:
  std::vector<std::unique_ptr<Tool>> tools_;
};

}  // namespace kasli::tools
```

Create `src/tools/tool_registry.cpp`:

```cpp
#include <kasli/tools/tool_registry.hpp>

#include <algorithm>

namespace kasli::tools {

void ToolRegistry::add(std::unique_ptr<Tool> tool) {
  tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(const std::string& name) const {
  const auto match = std::ranges::find_if(tools_, [&](const auto& tool) {
    return tool->name() == name;
  });
  if (match == tools_.end()) {
    return nullptr;
  }
  return match->get();
}

std::vector<std::string> ToolRegistry::names() const {
  std::vector<std::string> result;
  result.reserve(tools_.size());
  for (const auto& tool : tools_) {
    result.push_back(tool->name());
  }
  return result;
}

}  // namespace kasli::tools
```

- [ ] **Step 4: Add system info tool**

Create `include/kasli/tools/system_info_tool.hpp`:

```cpp
#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>

namespace kasli::tools {

class SystemInfoTool final : public Tool {
 public:
  explicit SystemInfoTool(std::filesystem::path os_release_path = "/etc/os-release");

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  std::filesystem::path os_release_path_;
};

}  // namespace kasli::tools
```

Create `src/tools/system_info_tool.cpp`:

```cpp
#include <kasli/tools/system_info_tool.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include <sys/utsname.h>

namespace kasli::tools {

SystemInfoTool::SystemInfoTool(std::filesystem::path os_release_path)
    : os_release_path_(std::move(os_release_path)) {}

std::string SystemInfoTool::name() const {
  return "system.info";
}

core::RiskClass SystemInfoTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse SystemInfoTool::call(const core::ToolRequest& request) const {
  std::ostringstream body;

  std::ifstream os_release(os_release_path_);
  if (os_release) {
    body << os_release.rdbuf();
  } else {
    body << "os_release=unavailable\n";
  }

  utsname uname_info{};
  if (uname(&uname_info) == 0) {
    body << "sysname=" << uname_info.sysname << '\n';
    body << "release=" << uname_info.release << '\n';
    body << "machine=" << uname_info.machine << '\n';
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "system info collected",
      .evidence = {core::Evidence{
          .id = request.id + ":system.info",
          .source = "system.info",
          .summary = "OS identity and kernel summary",
          .body = body.str(),
          .timestamp = "",
      }},
  };
}

}  // namespace kasli::tools
```

- [ ] **Step 5: Wire the tool code in CMake**

Modify the `kasli_core` target:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/policy/policy_broker.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

Add the tests:

```cmake
add_executable(kasli_tool_registry_test tests/unit/tool_registry_test.cpp)
target_link_libraries(kasli_tool_registry_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_tool_registry_test)

add_executable(kasli_system_info_tool_test tests/unit/system_info_tool_test.cpp)
target_link_libraries(kasli_system_info_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_system_info_tool_test)
```

- [ ] **Step 6: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```sh
git add CMakeLists.txt include/kasli/tools src/tools tests/fixtures tests/unit/tool_registry_test.cpp tests/unit/system_info_tool_test.cpp
git commit -m "feat: add read-only tool registry"
```

## Task 6: Session Service With Audit And Policy

**Files:**
- Create: `include/kasli/core/uuid.hpp`
- Create: `src/core/uuid.cpp`
- Create: `include/kasli/session/session_service.hpp`
- Create: `src/session/session_service.cpp`
- Create: `tests/unit/session_service_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing session tests**

Create `tests/unit/session_service_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/audit/audit_log.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include "../test_support/temp_dir.hpp"

#include <fstream>

TEST_CASE("session service executes allowed tool and audits it") {
  auto dir = kasli::test_support::temp_path("session_service_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>("tests/fixtures/os-release"));

  kasli::policy::PolicyBroker policy({"system.info"});
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
      .id = "req-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  }, "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);

  std::ifstream audit_file(dir / "audit.jsonl");
  std::string line;
  REQUIRE(std::getline(audit_file, line));
  REQUIRE(line.find("tool.call") != std::string::npos);
  REQUIRE(line.find("system.info") != std::string::npos);
}

TEST_CASE("session service denies unknown tool and audits denial") {
  auto dir = kasli::test_support::temp_path("session_service_denial_test");
  kasli::audit::AuditLog audit(dir / "audit.jsonl");
  kasli::tools::ToolRegistry registry;
  kasli::policy::PolicyBroker policy({"system.info"});
  kasli::session::SessionService service(registry, policy, audit);

  auto response = service.call_tool(kasli::core::ToolRequest{
      .id = "req-2",
      .tool_name = "shell.exec",
      .risk = kasli::core::RiskClass::Admin,
      .params = {},
  }, "tester");

  REQUIRE(response.status == kasli::core::ToolStatus::Denied);
  REQUIRE(response.message == "tool is not registered in policy allowlist");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R session
```

Expected: build fails because `kasli/session/session_service.hpp` does not exist.

- [ ] **Step 3: Add ID helper**

Create `include/kasli/core/uuid.hpp`:

```cpp
#pragma once

#include <string>

namespace kasli::core {

std::string make_event_id();

}  // namespace kasli::core
```

Create `src/core/uuid.cpp`:

```cpp
#include <kasli/core/uuid.hpp>

#include <atomic>
#include <chrono>
#include <sstream>

namespace kasli::core {

std::string make_event_id() {
  static std::atomic<unsigned long long> counter{0};
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();
  std::ostringstream output;
  output << "event-" << now << "-" << counter.fetch_add(1);
  return output.str();
}

}  // namespace kasli::core
```

- [ ] **Step 4: Implement session service**

Create `include/kasli/session/session_service.hpp`:

```cpp
#pragma once

#include <kasli/audit/audit_log.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <vector>

namespace kasli::session {

class SessionService {
 public:
  SessionService(const tools::ToolRegistry& registry,
                 const policy::PolicyBroker& policy,
                 const audit::AuditLog& audit);

  std::vector<std::string> list_tools() const;
  core::ToolResponse call_tool(const core::ToolRequest& request, const std::string& actor) const;

 private:
  const tools::ToolRegistry& registry_;
  const policy::PolicyBroker& policy_;
  const audit::AuditLog& audit_;
};

}  // namespace kasli::session
```

Create `src/session/session_service.cpp`:

```cpp
#include <kasli/session/session_service.hpp>

#include <kasli/core/uuid.hpp>

namespace kasli::session {

SessionService::SessionService(const tools::ToolRegistry& registry,
                               const policy::PolicyBroker& policy,
                               const audit::AuditLog& audit)
    : registry_(registry), policy_(policy), audit_(audit) {}

std::vector<std::string> SessionService::list_tools() const {
  return registry_.names();
}

core::ToolResponse SessionService::call_tool(const core::ToolRequest& request,
                                             const std::string& actor) const {
  const auto decision = policy_.decide(request);
  audit_.append(core::AuditEvent{
      .id = core::make_event_id(),
      .timestamp = "",
      .actor = actor,
      .type = "tool.call",
      .summary = request.tool_name,
      .details = {{"request_id", request.id}, {"decision", decision.reason}},
  });

  if (!decision.allowed) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Denied,
        .message = decision.reason,
        .evidence = {},
    };
  }

  const tools::Tool* tool = registry_.find(request.tool_name);
  if (tool == nullptr) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Denied,
        .message = "tool is not registered",
        .evidence = {},
    };
  }

  return tool->call(request);
}

}  // namespace kasli::session
```

- [ ] **Step 5: Wire session code in CMake**

Modify the `kasli_core` target:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

Add the session test:

```cmake
add_executable(kasli_session_service_test tests/unit/session_service_test.cpp)
target_link_libraries(kasli_session_service_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_session_service_test)
```

- [ ] **Step 6: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```sh
git add CMakeLists.txt include/kasli/core/uuid.hpp include/kasli/session src/core/uuid.cpp src/session tests/unit/session_service_test.cpp
git commit -m "feat: add audited session service"
```

## Task 7: Line Protocol, Daemon, And CLI Commands

**Files:**
- Create: `include/kasli/ipc/line_protocol.hpp`
- Create: `src/ipc/line_protocol.cpp`
- Create: `src/kaslid/main.cpp`
- Create: `src/kasli-cli/main.cpp`
- Create: `tests/unit/line_protocol_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing line protocol tests**

Create `tests/unit/line_protocol_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/ipc/line_protocol.hpp>

#include <nlohmann/json.hpp>

TEST_CASE("line protocol encodes tools list request") {
  auto request = kasli::ipc::make_tools_list_request("req-1");
  REQUIRE(request.at("id") == "req-1");
  REQUIRE(request.at("method") == "tools.list");
}

TEST_CASE("line protocol encodes tool call request") {
  kasli::core::ToolRequest call{
      .id = "tool-1",
      .tool_name = "system.info",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {},
  };

  auto request = kasli::ipc::make_tool_call_request("req-2", call);
  REQUIRE(request.at("method") == "tool.call");
  REQUIRE(request.at("tool").at("tool_name") == "system.info");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R line_protocol
```

Expected: build fails because `kasli/ipc/line_protocol.hpp` does not exist.

- [ ] **Step 3: Implement line protocol helpers**

Create `include/kasli/ipc/line_protocol.hpp`:

```cpp
#pragma once

#include <kasli/core/types.hpp>

#include <nlohmann/json.hpp>
#include <string>

namespace kasli::ipc {

nlohmann::json make_tools_list_request(const std::string& request_id);
nlohmann::json make_tool_call_request(const std::string& request_id, const core::ToolRequest& tool_request);
nlohmann::json make_error_response(const std::string& request_id, const std::string& message);

}  // namespace kasli::ipc
```

Create `src/ipc/line_protocol.cpp`:

```cpp
#include <kasli/ipc/line_protocol.hpp>

namespace kasli::ipc {

nlohmann::json make_tools_list_request(const std::string& request_id) {
  return nlohmann::json{{"id", request_id}, {"method", "tools.list"}};
}

nlohmann::json make_tool_call_request(const std::string& request_id,
                                      const core::ToolRequest& tool_request) {
  return nlohmann::json{{"id", request_id}, {"method", "tool.call"}, {"tool", tool_request}};
}

nlohmann::json make_error_response(const std::string& request_id, const std::string& message) {
  return nlohmann::json{{"id", request_id}, {"ok", false}, {"error", message}};
}

}  // namespace kasli::ipc
```

- [ ] **Step 4: Add temporary foreground daemon JSON mode**

Create `src/kaslid/main.cpp`:

```cpp
#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

int main(int argc, char** argv) {
  std::filesystem::path audit_path = "kasli-audit.jsonl";
  if (argc == 3 && std::string(argv[1]) == "--audit-log") {
    audit_path = argv[2];
  }

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>());

  kasli::policy::PolicyBroker policy(registry.names());
  kasli::audit::AuditLog audit(audit_path);
  kasli::session::SessionService service(registry, policy, audit);

  std::string line;
  while (std::getline(std::cin, line)) {
    try {
      auto request = nlohmann::json::parse(line);
      const std::string id = request.at("id").get<std::string>();
      const std::string method = request.at("method").get<std::string>();

      if (method == "tools.list") {
        std::cout << nlohmann::json{{"id", id}, {"ok", true}, {"tools", service.list_tools()}}.dump() << '\n';
        continue;
      }

      if (method == "tool.call") {
        auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
        auto response = service.call_tool(tool_request, "cli");
        std::cout << nlohmann::json{{"id", id}, {"ok", true}, {"response", response}}.dump() << '\n';
        continue;
      }

      std::cout << kasli::ipc::make_error_response(id, "unknown method").dump() << '\n';
    } catch (const std::exception& error) {
      std::cout << kasli::ipc::make_error_response("unknown", error.what()).dump() << '\n';
    }
  }

  return 0;
}
```

- [ ] **Step 5: Add temporary CLI request generation mode**

Create `src/kasli-cli/main.cpp`:

```cpp
#include <CLI/CLI.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>

#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{"Kasli read-only system assistant CLI"};

  bool list_tools = false;
  std::string call_tool;
  app.add_flag("--tools-list", list_tools, "Print a tools.list JSON request");
  app.add_option("--call-tool", call_tool, "Print a tool.call JSON request for a read-only tool");

  CLI11_PARSE(app, argc, argv);

  if (list_tools) {
    std::cout << kasli::ipc::make_tools_list_request("cli-1").dump() << '\n';
    return 0;
  }

  if (!call_tool.empty()) {
    kasli::core::ToolRequest request{
        .id = "cli-tool-1",
        .tool_name = call_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    };
    std::cout << kasli::ipc::make_tool_call_request("cli-2", request).dump() << '\n';
    return 0;
  }

  std::cerr << app.help() << '\n';
  return 1;
}
```

- [ ] **Step 6: Wire IPC, daemon, and CLI in CMake**

Modify `kasli_core`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

Add executables and the line protocol test:

```cmake
add_executable(kaslid src/kaslid/main.cpp)
target_link_libraries(kaslid PRIVATE kasli_core)

add_executable(kasli src/kasli-cli/main.cpp)
target_link_libraries(kasli PRIVATE kasli_core CLI11::CLI11)

add_executable(kasli_line_protocol_test tests/unit/line_protocol_test.cpp)
target_link_libraries(kasli_line_protocol_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_line_protocol_test)
```

- [ ] **Step 7: Run tests and manual daemon check**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
printf '%s\n' "$(./build/kasli --tools-list)" | ./build/kaslid --audit-log build/dev-audit.jsonl
printf '%s\n' "$(./build/kasli --call-tool system.info)" | ./build/kaslid --audit-log build/dev-audit.jsonl
```

Expected: tests pass. The manual checks print JSON responses. The second response contains `"source":"system.info"` inside the response evidence.

- [ ] **Step 8: Commit**

```sh
git add CMakeLists.txt include/kasli/ipc src/ipc src/kaslid src/kasli-cli tests/unit/line_protocol_test.cpp
git commit -m "feat: add daemon JSON protocol and CLI"
```

## Task 8: Unix Domain Socket Transport

**Files:**
- Create: `include/kasli/ipc/unix_socket.hpp`
- Create: `src/ipc/unix_socket.cpp`
- Create: `tests/integration/unix_socket_test.cpp`
- Modify: `src/kaslid/main.cpp`
- Modify: `src/kasli-cli/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing Unix socket integration test**

Create `tests/integration/unix_socket_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include "../test_support/temp_dir.hpp"

#include <thread>

TEST_CASE("unix socket client sends request and receives response") {
  const auto dir = kasli::test_support::temp_path("unix_socket_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);
  std::thread server_thread([&] {
    server.accept_one([](const std::string& request) {
      return std::string{"response:"} + request;
    });
  });

  const auto response = kasli::ipc::request_over_unix_socket(socket_path, "ping");
  server_thread.join();

  REQUIRE(response == "response:ping");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R unix_socket
```

Expected: build fails because `kasli/ipc/unix_socket.hpp` does not exist.

- [ ] **Step 3: Implement Unix socket helpers**

Create `include/kasli/ipc/unix_socket.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace kasli::ipc {

class UnixSocketServer {
 public:
  explicit UnixSocketServer(std::filesystem::path socket_path);
  ~UnixSocketServer();

  UnixSocketServer(const UnixSocketServer&) = delete;
  UnixSocketServer& operator=(const UnixSocketServer&) = delete;

  void accept_one(const std::function<std::string(const std::string&)>& handler) const;

 private:
  int fd_ = -1;
  std::filesystem::path socket_path_;
};

std::string request_over_unix_socket(const std::filesystem::path& socket_path,
                                     const std::string& request);

}  // namespace kasli::ipc
```

Create `src/ipc/unix_socket.cpp`:

```cpp
#include <kasli/ipc/unix_socket.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace kasli::ipc {

namespace {

void throw_errno(const std::string& message) {
  throw std::runtime_error(message + ": " + std::strerror(errno));
}

void write_all(int fd, const std::string& data) {
  const char* cursor = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    const ssize_t written = ::write(fd, cursor, remaining);
    if (written < 0) {
      throw_errno("socket write failed");
    }
    cursor += written;
    remaining -= static_cast<size_t>(written);
  }
}

std::string read_all(int fd) {
  std::string result;
  std::array<char, 4096> buffer{};
  while (true) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0) {
      throw_errno("socket read failed");
    }
    if (count == 0) {
      return result;
    }
    result.append(buffer.data(), static_cast<size_t>(count));
  }
}

sockaddr_un make_address(const std::filesystem::path& socket_path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const auto path_string = socket_path.string();
  if (path_string.size() >= sizeof(address.sun_path)) {
    throw std::runtime_error("socket path is too long");
  }
  std::strncpy(address.sun_path, path_string.c_str(), sizeof(address.sun_path) - 1);
  return address;
}

}  // namespace

UnixSocketServer::UnixSocketServer(std::filesystem::path socket_path)
    : socket_path_(std::move(socket_path)) {
  if (socket_path_.has_parent_path()) {
    std::filesystem::create_directories(socket_path_.parent_path());
  }
  std::filesystem::remove(socket_path_);

  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) {
    throw_errno("socket creation failed");
  }

  auto address = make_address(socket_path_);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    throw_errno("socket bind failed");
  }
  if (::listen(fd_, 8) < 0) {
    throw_errno("socket listen failed");
  }
}

UnixSocketServer::~UnixSocketServer() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
  std::filesystem::remove(socket_path_);
}

void UnixSocketServer::accept_one(const std::function<std::string(const std::string&)>& handler) const {
  const int client = ::accept(fd_, nullptr, nullptr);
  if (client < 0) {
    throw_errno("socket accept failed");
  }
  const std::string request = read_all(client);
  const std::string response = handler(request);
  write_all(client, response);
  ::close(client);
}

std::string request_over_unix_socket(const std::filesystem::path& socket_path,
                                     const std::string& request) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    throw_errno("socket creation failed");
  }

  auto address = make_address(socket_path);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    ::close(fd);
    throw_errno("socket connect failed");
  }

  write_all(fd, request);
  ::shutdown(fd, SHUT_WR);
  const std::string response = read_all(fd);
  ::close(fd);
  return response;
}

}  // namespace kasli::ipc
```

- [ ] **Step 4: Replace daemon main with socket server mode**

Replace `src/kaslid/main.cpp` with:

```cpp
#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/ipc/unix_socket.hpp>
#include <kasli/policy/policy_broker.hpp>
#include <kasli/session/session_service.hpp>
#include <kasli/tools/system_info_tool.hpp>
#include <kasli/tools/tool_registry.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json handle_request(const kasli::session::SessionService& service,
                              const std::string& line) {
  auto request = nlohmann::json::parse(line);
  const std::string id = request.at("id").get<std::string>();
  const std::string method = request.at("method").get<std::string>();

  if (method == "tools.list") {
    return nlohmann::json{{"id", id}, {"ok", true}, {"tools", service.list_tools()}};
  }

  if (method == "tool.call") {
    auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
    auto response = service.call_tool(tool_request, "cli");
    return nlohmann::json{{"id", id}, {"ok", true}, {"response", response}};
  }

  return kasli::ipc::make_error_response(id, "unknown method");
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path socket_path = "/tmp/kaslid.sock";
  std::filesystem::path audit_path = "kasli-audit.jsonl";
  bool once = false;

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--socket" && index + 1 < argc) {
      socket_path = argv[++index];
    } else if (arg == "--audit-log" && index + 1 < argc) {
      audit_path = argv[++index];
    } else if (arg == "--once") {
      once = true;
    }
  }

  kasli::tools::ToolRegistry registry;
  registry.add(std::make_unique<kasli::tools::SystemInfoTool>());

  kasli::policy::PolicyBroker policy(registry.names());
  kasli::audit::AuditLog audit(audit_path);
  kasli::session::SessionService service(registry, policy, audit);
  kasli::ipc::UnixSocketServer server(socket_path);

  do {
    server.accept_one([&](const std::string& request) {
      try {
        return handle_request(service, request).dump();
      } catch (const std::exception& error) {
        return kasli::ipc::make_error_response("unknown", error.what()).dump();
      }
    });
  } while (!once);

  return 0;
}
```

- [ ] **Step 5: Replace CLI main with socket client mode**

Replace `src/kasli-cli/main.cpp` with:

```cpp
#include <CLI/CLI.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  CLI::App app{"Kasli read-only system assistant CLI"};

  std::filesystem::path socket_path = "/tmp/kaslid.sock";
  bool list_tools = false;
  std::string call_tool;

  app.add_option("--socket", socket_path, "kaslid Unix domain socket path");
  app.add_flag("--tools-list", list_tools, "Ask daemon to list tools");
  app.add_option("--call-tool", call_tool, "Ask daemon to call a read-only tool");

  CLI11_PARSE(app, argc, argv);

  nlohmann::json request;
  if (list_tools) {
    request = kasli::ipc::make_tools_list_request("cli-1");
  } else if (!call_tool.empty()) {
    request = kasli::ipc::make_tool_call_request("cli-2", kasli::core::ToolRequest{
        .id = "cli-tool-1",
        .tool_name = call_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    });
  } else {
    std::cerr << app.help() << '\n';
    return 1;
  }

  std::cout << kasli::ipc::request_over_unix_socket(socket_path, request.dump()) << '\n';
  return 0;
}
```

- [ ] **Step 6: Wire socket code in CMake**

Modify `kasli_core`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/ipc/unix_socket.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

Add the integration test:

```cmake
add_executable(kasli_unix_socket_test tests/integration/unix_socket_test.cpp)
target_link_libraries(kasli_unix_socket_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_unix_socket_test)
```

- [ ] **Step 7: Run tests and manual socket checks**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --tools-list
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool system.info
```

Expected: tests pass. The manual checks print JSON responses over the Unix domain socket. The second response contains `"source":"system.info"` inside the response evidence.

- [ ] **Step 8: Commit**

```sh
git add CMakeLists.txt include/kasli/ipc/unix_socket.hpp src/ipc/unix_socket.cpp src/kaslid/main.cpp src/kasli-cli/main.cpp tests/integration/unix_socket_test.cpp
git commit -m "feat: add Unix socket daemon transport"
```

## Task 9: Journal Fixture Tool And Redaction

**Files:**
- Create: `include/kasli/tools/journal_tool.hpp`
- Create: `src/tools/journal_tool.cpp`
- Create: `tests/fixtures/journal/ssh_failed.jsonl`
- Create: `tests/unit/journal_tool_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing journal fixture test**

Create `tests/fixtures/journal/ssh_failed.jsonl`:

```jsonl
{"_SYSTEMD_UNIT":"ssh.service","PRIORITY":"3","MESSAGE":"Failed to start OpenSSH server daemon"}
{"_SYSTEMD_UNIT":"ssh.service","PRIORITY":"4","MESSAGE":"Bind to port 22 failed: Address already in use"}
{"_SYSTEMD_UNIT":"ssh.service","PRIORITY":"6","MESSAGE":"debug token=should_not_leave_context"}
```

Create `tests/unit/journal_tool_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/journal_tool.hpp>

TEST_CASE("journal fixture tool filters unit and redacts secrets") {
  kasli::tools::JournalFixtureTool tool("tests/fixtures/journal/ssh_failed.jsonl");
  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-1",
      .tool_name = "journal.query",
      .risk = kasli::core::RiskClass::ReadOnly,
      .params = {{"unit", "ssh.service"}},
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.at(0).body.find("Address already in use") != std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("should_not_leave_context") == std::string::npos);
  REQUIRE(response.evidence.at(0).body.find("[REDACTED]") != std::string::npos);
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R journal
```

Expected: build fails because `kasli/tools/journal_tool.hpp` does not exist.

- [ ] **Step 3: Implement fixture-backed journal query**

Create `include/kasli/tools/journal_tool.hpp`:

```cpp
#pragma once

#include <kasli/tools/tool.hpp>

#include <filesystem>

namespace kasli::tools {

class JournalFixtureTool final : public Tool {
 public:
  explicit JournalFixtureTool(std::filesystem::path fixture_path);

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  std::filesystem::path fixture_path_;
};

}  // namespace kasli::tools
```

Create `src/tools/journal_tool.cpp`:

```cpp
#include <kasli/tools/journal_tool.hpp>

#include <kasli/core/json.hpp>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace kasli::tools {

JournalFixtureTool::JournalFixtureTool(std::filesystem::path fixture_path)
    : fixture_path_(std::move(fixture_path)) {}

std::string JournalFixtureTool::name() const {
  return "journal.query";
}

core::RiskClass JournalFixtureTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse JournalFixtureTool::call(const core::ToolRequest& request) const {
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";
  std::ifstream input(fixture_path_);
  std::ostringstream body;

  std::string line;
  while (std::getline(input, line)) {
    auto entry = nlohmann::json::parse(line);
    if (!unit.empty() && entry.value("_SYSTEMD_UNIT", "") != unit) {
      continue;
    }
    const auto message = core::redact_likely_secret(entry.value("MESSAGE", ""));
    body << entry.value("PRIORITY", "") << " " << message << '\n';
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "journal fixture queried",
      .evidence = {core::Evidence{
          .id = request.id + ":journal.query",
          .source = "journal.query",
          .summary = "bounded journal entries for " + unit,
          .body = body.str(),
          .timestamp = "",
      }},
  };
}

}  // namespace kasli::tools
```

- [ ] **Step 4: Wire journal tool in CMake and daemon registry**

Modify `kasli_core`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/ipc/unix_socket.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/journal_tool.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

Add the journal test:

```cmake
add_executable(kasli_journal_tool_test tests/unit/journal_tool_test.cpp)
target_link_libraries(kasli_journal_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_journal_tool_test)
```

In `src/kaslid/main.cpp`, include the journal header:

```cpp
#include <kasli/tools/journal_tool.hpp>
```

In `src/kaslid/main.cpp`, add this registry entry after `SystemInfoTool`:

```cpp
  registry.add(std::make_unique<kasli::tools::JournalFixtureTool>("tests/fixtures/journal/ssh_failed.jsonl"));
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. The journal test proves bounded evidence and redaction behavior before live journal integration.

- [ ] **Step 6: Commit**

```sh
git add CMakeLists.txt include/kasli/tools/journal_tool.hpp src/tools/journal_tool.cpp src/kaslid/main.cpp tests/fixtures/journal tests/unit/journal_tool_test.cpp
git commit -m "feat: add bounded journal evidence tool"
```

## Task 10: Model Provider Interface And Fake Answer Path

**Files:**
- Create: `include/kasli/model/model_provider.hpp`
- Create: `tests/test_support/fake_model_provider.hpp`
- Create: `tests/unit/model_context_test.cpp`
- Modify: `include/kasli/session/session_service.hpp`
- Modify: `src/session/session_service.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing model context test**

Create `tests/test_support/fake_model_provider.hpp`:

```cpp
#pragma once

#include <kasli/model/model_provider.hpp>

namespace kasli::test_support {

class FakeModelProvider final : public model::ModelProvider {
 public:
  std::string complete(const model::ModelRequest& request) const override {
    return "Fake answer based on " + std::to_string(request.evidence.size()) + " evidence record(s).";
  }
};

}  // namespace kasli::test_support
```

Create `tests/unit/model_context_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <kasli/model/model_provider.hpp>

#include "../test_support/fake_model_provider.hpp"

TEST_CASE("fake model receives curated evidence only") {
  kasli::test_support::FakeModelProvider provider;
  kasli::model::ModelRequest request{
      .prompt = "Why did ssh fail?",
      .evidence = {kasli::core::Evidence{
          .id = "ev-1",
          .source = "journal.query",
          .summary = "ssh journal",
          .body = "Bind to port 22 failed",
          .timestamp = "",
      }},
  };

  REQUIRE(provider.complete(request) == "Fake answer based on 1 evidence record(s).");
}
```

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R model_context
```

Expected: build fails because `kasli/model/model_provider.hpp` does not exist.

- [ ] **Step 3: Add model provider interface**

Create `include/kasli/model/model_provider.hpp`:

```cpp
#pragma once

#include <kasli/core/types.hpp>

#include <string>
#include <vector>

namespace kasli::model {

struct ModelRequest {
  std::string prompt;
  std::vector<core::Evidence> evidence;
};

class ModelProvider {
 public:
  virtual ~ModelProvider() = default;
  virtual std::string complete(const ModelRequest& request) const = 0;
};

}  // namespace kasli::model
```

- [ ] **Step 4: Add `ask_with_tool` orchestration**

Modify `include/kasli/session/session_service.hpp` to include the model provider:

```cpp
#include <kasli/model/model_provider.hpp>
```

Add this public method to `SessionService`:

```cpp
  std::string ask_with_tool(const std::string& prompt,
                            const core::ToolRequest& request,
                            const model::ModelProvider& model,
                            const std::string& actor) const;
```

Add this implementation to `src/session/session_service.cpp`:

```cpp
std::string SessionService::ask_with_tool(const std::string& prompt,
                                          const core::ToolRequest& request,
                                          const model::ModelProvider& model,
                                          const std::string& actor) const {
  auto tool_response = call_tool(request, actor);
  audit_.append(core::AuditEvent{
      .id = core::make_event_id(),
      .timestamp = "",
      .actor = actor,
      .type = "model.request",
      .summary = prompt,
      .details = {{"request_id", request.id}, {"evidence_count", std::to_string(tool_response.evidence.size())}},
  });
  return model.complete(model::ModelRequest{.prompt = prompt, .evidence = tool_response.evidence});
}
```

- [ ] **Step 5: Wire model context test in CMake**

Add:

```cmake
add_executable(kasli_model_context_test tests/unit/model_context_test.cpp)
target_link_libraries(kasli_model_context_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_model_context_test)
```

- [ ] **Step 6: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```sh
git add CMakeLists.txt include/kasli/model include/kasli/session src/session tests/test_support/fake_model_provider.hpp tests/unit/model_context_test.cpp
git commit -m "feat: add model provider boundary"
```

## Task 11: Ollama Provider And `ask` CLI Command

**Files:**
- Create: `include/kasli/model/ollama_provider.hpp`
- Create: `src/model/ollama_provider.cpp`
- Modify: `cmake/KasliDependencies.cmake`
- Modify: `CMakeLists.txt`
- Modify: `src/kasli-cli/main.cpp`

- [ ] **Step 1: Add libcurl discovery**

Append to `cmake/KasliDependencies.cmake`:

```cmake
find_package(CURL)
```

Append to `CMakeLists.txt` after `kasli_core`:

```cmake
if(CURL_FOUND AND KASLI_ENABLE_CURL)
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_CURL=1)
  target_link_libraries(kasli_core PUBLIC CURL::libcurl)
else()
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_CURL=0)
endif()
```

- [ ] **Step 2: Add Ollama provider**

Create `include/kasli/model/ollama_provider.hpp`:

```cpp
#pragma once

#include <kasli/model/model_provider.hpp>

#include <string>

namespace kasli::model {

class OllamaProvider final : public ModelProvider {
 public:
  OllamaProvider(std::string endpoint, std::string model);

  std::string complete(const ModelRequest& request) const override;

 private:
  std::string endpoint_;
  std::string model_;
};

}  // namespace kasli::model
```

Create `src/model/ollama_provider.cpp`:

```cpp
#include <kasli/model/ollama_provider.hpp>

#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#if KASLI_HAS_CURL
#include <curl/curl.h>
#endif

namespace kasli::model {

namespace {

#if KASLI_HAS_CURL
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* output = static_cast<std::string*>(userdata);
  output->append(ptr, size * nmemb);
  return size * nmemb;
}
#endif

std::string build_prompt(const ModelRequest& request) {
  std::ostringstream prompt;
  prompt << request.prompt << "\n\nEvidence:\n";
  for (const auto& evidence : request.evidence) {
    prompt << "[" << evidence.id << "] " << evidence.source << " - " << evidence.summary << "\n";
    prompt << evidence.body << "\n";
  }
  prompt << "\nAnswer using only the evidence above. Say when evidence is insufficient.";
  return prompt.str();
}

}  // namespace

OllamaProvider::OllamaProvider(std::string endpoint, std::string model)
    : endpoint_(std::move(endpoint)), model_(std::move(model)) {}

std::string OllamaProvider::complete(const ModelRequest& request) const {
#if KASLI_HAS_CURL
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl initialization failed");
  }

  std::string response_body;
  const nlohmann::json payload = {
      {"model", model_},
      {"prompt", build_prompt(request)},
      {"stream", false},
  };

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, (endpoint_ + "/api/generate").c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.dump().c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  const CURLcode result = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (result != CURLE_OK) {
    throw std::runtime_error("ollama request failed");
  }

  auto json = nlohmann::json::parse(response_body);
  return json.value("response", "");
#else
  (void)request;
  throw std::runtime_error("ollama provider requires libcurl");
#endif
}

}  // namespace kasli::model
```

- [ ] **Step 3: Wire source file in CMake**

Add `src/model/ollama_provider.cpp` to `kasli_core`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/ipc/unix_socket.cpp
  src/model/ollama_provider.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/journal_tool.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
```

- [ ] **Step 4: Extend CLI with socket-backed `ask` command**

Replace `src/kasli-cli/main.cpp` so it supports `ask` through the Unix socket:

```cpp
#include <CLI/CLI.hpp>
#include <kasli/core/types.hpp>
#include <kasli/ipc/line_protocol.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char** argv) {
  CLI::App app{"Kasli read-only system assistant CLI"};

  std::filesystem::path socket_path = "/tmp/kaslid.sock";
  bool list_tools = false;
  std::string call_tool;
  std::string ask_prompt;
  std::string ask_tool = "system.info";

  app.add_option("--socket", socket_path, "kaslid Unix domain socket path");
  app.add_flag("--tools-list", list_tools, "Ask daemon to list tools");
  app.add_option("--call-tool", call_tool, "Ask daemon to call a read-only tool");
  app.add_option("--ask", ask_prompt, "Ask daemon to answer using a read-only tool");
  app.add_option("--ask-tool", ask_tool, "Read-only tool to use for the ask request");

  CLI11_PARSE(app, argc, argv);

  nlohmann::json request_json;
  if (list_tools) {
    request_json = kasli::ipc::make_tools_list_request("cli-1");
  } else if (!call_tool.empty()) {
    request_json = kasli::ipc::make_tool_call_request("cli-2", kasli::core::ToolRequest{
        .id = "cli-tool-1",
        .tool_name = call_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    });
  } else if (!ask_prompt.empty()) {
    kasli::core::ToolRequest request{
        .id = "cli-ask-tool-1",
        .tool_name = ask_tool,
        .risk = kasli::core::RiskClass::ReadOnly,
        .params = {},
    };
    request_json = nlohmann::json{
        {"id", "cli-ask-1"},
        {"method", "ask"},
        {"prompt", ask_prompt},
        {"tool", request},
    };
  } else {
    std::cerr << app.help() << '\n';
    return 1;
  }

  std::cout << kasli::ipc::request_over_unix_socket(socket_path, request_json.dump()) << '\n';
  return 0;
}
```

- [ ] **Step 5: Extend daemon with `ask` method**

In `src/kaslid/main.cpp`, include the Ollama provider:

```cpp
#include <kasli/model/ollama_provider.hpp>
```

Before the `unknown method` response in `handle_request`, add:

```cpp
  if (method == "ask") {
    const auto prompt = request.at("prompt").get<std::string>();
    auto tool_request = request.at("tool").get<kasli::core::ToolRequest>();
    kasli::model::OllamaProvider model("http://127.0.0.1:11434", "llama3.2");
    const auto answer = service.ask_with_tool(prompt, tool_request, model, "cli");
    return nlohmann::json{{"id", id}, {"ok", true}, {"answer", answer}};
  }
```

- [ ] **Step 6: Run tests and manual ask command**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --ask 'What OS is this?' --ask-tool system.info
```

Expected: tests pass. If Ollama is not running, the daemon prints an error response. If Ollama is running with `llama3.2` available, it prints a JSON answer derived from `system.info` evidence.

- [ ] **Step 7: Commit**

```sh
git add CMakeLists.txt cmake/KasliDependencies.cmake include/kasli/model src/model src/kasli-cli/main.cpp src/kaslid/main.cpp
git commit -m "feat: add Ollama model provider"
```

## Task 12: Live Linux Systemd And Journal Adapters

**Files:**
- Create: `include/kasli/tools/systemd_tool.hpp`
- Create: `src/tools/systemd_tool.cpp`
- Modify: `include/kasli/tools/journal_tool.hpp`
- Modify: `src/tools/journal_tool.cpp`
- Modify: `cmake/KasliDependencies.cmake`
- Modify: `CMakeLists.txt`
- Modify: `src/kaslid/main.cpp`

- [ ] **Step 1: Add libsystemd discovery**

Append to `cmake/KasliDependencies.cmake`:

```cmake
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBSYSTEMD libsystemd)
endif()
```

Append to `CMakeLists.txt` after the libcurl section:

```cmake
if(LIBSYSTEMD_FOUND AND KASLI_ENABLE_SYSTEMD)
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_SYSTEMD=1)
  target_include_directories(kasli_core PUBLIC ${LIBSYSTEMD_INCLUDE_DIRS})
  target_link_libraries(kasli_core PUBLIC ${LIBSYSTEMD_LIBRARIES})
else()
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_SYSTEMD=0)
endif()
```

- [ ] **Step 2: Add live systemd unit list tool**

Create `include/kasli/tools/systemd_tool.hpp`:

```cpp
#pragma once

#include <kasli/tools/tool.hpp>

namespace kasli::tools {

class SystemdUnitsTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};

}  // namespace kasli::tools
```

Create `src/tools/systemd_tool.cpp`:

```cpp
#include <kasli/tools/systemd_tool.hpp>

#include <sstream>
#include <stdexcept>

#if KASLI_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace kasli::tools {

std::string SystemdUnitsTool::name() const {
  return "systemd.units.list";
}

core::RiskClass SystemdUnitsTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse SystemdUnitsTool::call(const core::ToolRequest& request) const {
#if KASLI_HAS_SYSTEMD
  sd_bus* bus = nullptr;
  sd_bus_message* reply = nullptr;
  sd_bus_error error = SD_BUS_ERROR_NULL;

  if (sd_bus_open_system(&bus) < 0) {
    throw std::runtime_error("failed to open system bus");
  }

  const int call_result = sd_bus_call_method(
      bus,
      "org.freedesktop.systemd1",
      "/org/freedesktop/systemd1",
      "org.freedesktop.systemd1.Manager",
      "ListUnits",
      &error,
      &reply,
      "");
  if (call_result < 0) {
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    throw std::runtime_error("ListUnits failed");
  }

  std::ostringstream body;
  sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
  const char* name = nullptr;
  const char* description = nullptr;
  const char* load_state = nullptr;
  const char* active_state = nullptr;
  const char* sub_state = nullptr;
  const char* followed = nullptr;
  const char* unit_path = nullptr;
  const char* job_type = nullptr;
  const char* job_path = nullptr;
  uint32_t job_id = 0;

  int rows = 0;
  while (sd_bus_message_read(reply, "(ssssssouso)", &name, &description, &load_state,
                             &active_state, &sub_state, &followed, &unit_path,
                             &job_id, &job_type, &job_path) > 0) {
    body << name << " " << active_state << " " << sub_state << " " << description << '\n';
    rows += 1;
    if (rows >= 200) {
      body << "truncated=true\n";
      break;
    }
  }
  sd_bus_message_exit_container(reply);

  sd_bus_message_unref(reply);
  sd_bus_unref(bus);

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "systemd units listed",
      .evidence = {core::Evidence{
          .id = request.id + ":systemd.units.list",
          .source = "systemd.units.list",
          .summary = "loaded systemd units",
          .body = body.str(),
          .timestamp = "",
      }},
  };
#else
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "systemd support was not built",
      .evidence = {},
  };
#endif
}

}  // namespace kasli::tools
```

- [ ] **Step 3: Add live journal mode to journal tool**

Modify `include/kasli/tools/journal_tool.hpp` by adding this class:

```cpp
class LiveJournalTool final : public Tool {
 public:
  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;
};
```

Append this implementation to `src/tools/journal_tool.cpp`:

```cpp
#if KASLI_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#endif

std::string LiveJournalTool::name() const {
  return "journal.query";
}

core::RiskClass LiveJournalTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse LiveJournalTool::call(const core::ToolRequest& request) const {
#if KASLI_HAS_SYSTEMD
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";
  sd_journal* journal = nullptr;
  if (sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY) < 0) {
    throw std::runtime_error("failed to open journal");
  }

  std::string match = "_SYSTEMD_UNIT=" + unit;
  if (!unit.empty()) {
    sd_journal_add_match(journal, match.c_str(), 0);
  }
  sd_journal_seek_tail(journal);

  std::ostringstream body;
  int count = 0;
  while (count < 50 && sd_journal_previous(journal) > 0) {
    const void* data = nullptr;
    size_t length = 0;
    if (sd_journal_get_data(journal, "MESSAGE", &data, &length) == 0) {
      std::string field(static_cast<const char*>(data), length);
      const std::string prefix = "MESSAGE=";
      if (field.starts_with(prefix)) {
        body << core::redact_likely_secret(field.substr(prefix.size())) << '\n';
      }
    }
    count += 1;
  }

  sd_journal_close(journal);
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "journal queried",
      .evidence = {core::Evidence{
          .id = request.id + ":journal.query",
          .source = "journal.query",
          .summary = "recent journal entries for " + unit,
          .body = body.str(),
          .timestamp = "",
      }},
  };
#else
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "journal support was not built",
      .evidence = {},
  };
#endif
}
```

- [ ] **Step 4: Register live tools in daemon when available**

In `src/kaslid/main.cpp`, include:

```cpp
#include <kasli/tools/systemd_tool.hpp>
```

Replace the fixture journal registration with:

```cpp
#if KASLI_HAS_SYSTEMD
  registry.add(std::make_unique<kasli::tools::SystemdUnitsTool>());
  registry.add(std::make_unique<kasli::tools::LiveJournalTool>());
#else
  registry.add(std::make_unique<kasli::tools::JournalFixtureTool>("tests/fixtures/journal/ssh_failed.jsonl"));
#endif
```

- [ ] **Step 5: Wire systemd source file in CMake**

Add `src/tools/systemd_tool.cpp` to `kasli_core`:

```cmake
add_library(kasli_core
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/ipc/unix_socket.cpp
  src/model/ollama_provider.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/journal_tool.cpp
  src/tools/system_info_tool.cpp
  src/tools/systemd_tool.cpp
  src/tools/tool_registry.cpp
)
```

- [ ] **Step 6: Run tests and Linux manual checks**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --call-tool systemd.units.list
```

Expected: tests pass. On Linux with libsystemd, the manual check returns up to 200 loaded units. On non-Linux or without libsystemd, the build still succeeds and the daemon uses the fixture-backed journal tool.

- [ ] **Step 7: Commit**

```sh
git add CMakeLists.txt cmake/KasliDependencies.cmake include/kasli/tools/systemd_tool.hpp include/kasli/tools/journal_tool.hpp src/tools/systemd_tool.cpp src/tools/journal_tool.cpp src/kaslid/main.cpp
git commit -m "feat: add live systemd read-only adapters"
```

## Task 13: Final Verification And Documentation Note

**Files:**
- Create: `docs/research/prototype-notes.md`
- Modify: `docs/superpowers/plans/2026-04-26-read-only-core-prototype.md`

- [x] **Step 1: Create prototype notes**

Create `docs/research/prototype-notes.md`:

````markdown
# Read-Only Prototype Notes

The first prototype implements a distro-neutral C++ read-only management layer.

Implemented trust boundaries:

- No unrestricted shell execution.
- Tool calls are typed.
- Tool risk is checked by the policy broker.
- Only read-only tools are allowed.
- Audit events are written as JSONL.
- Model requests receive curated evidence only.

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

Ollama-backed `ask` requires Ollama to be running locally with the `llama3.2` model available. If Ollama is not running or the model is unavailable, `ask` returns a JSON error response and audits `model.error`.

If Ollama is running with `llama3.2` available:

```sh
./build/kaslid --socket build/kaslid.sock --audit-log build/dev-audit.jsonl --once &
sleep 1
./build/kasli --socket build/kaslid.sock --ask 'What OS is this?' --ask-tool system.info
```
````

- [x] **Step 2: Run final verification**

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
git status --short
```

Expected: CMake configures, build succeeds, tests pass, and `git status --short` shows only files intentionally created or modified for Task 13.

- [x] **Step 3: Commit final notes**

```sh
git add docs/research/prototype-notes.md docs/superpowers/plans/2026-04-26-read-only-core-prototype.md
git commit -m "docs: record read-only prototype verification"
```

## Plan Self-Review

Spec coverage:

- Research and base OS choice are documented in the approved design spec, not reimplemented here.
- The no-mutation rule is covered by Tasks 4, 6, 7, 8, 9, 10, and 12.
- Typed tool registry is covered by Task 5.
- CLI and daemon request flow are covered by Tasks 7 and 8.
- The Unix domain socket daemon boundary required by the design spec is covered by Task 8.
- Audit logging is covered by Task 3 and integrated in Task 6.
- Curated model context is covered by Task 10 and Ollama support by Task 11.
- Read-only system state is covered by Task 5, fixture journal in Task 9, and live Linux adapters in Task 12.
- Tests are introduced before implementation for the prototype tasks where feasible; the Task 12 live systemd code path was statically inspected on macOS, with unavailable-path tests covering non-systemd builds.

Blocked-marker scan:

- No task contains blocked marker words or unbounded fill-in work.
- Commands include expected results.
- Code names introduced in later tasks are defined in earlier tasks or the same task.

Type consistency:

- `RiskClass`, `ToolRequest`, `ToolResponse`, `Evidence`, and `AuditEvent` are defined in Task 2 and reused consistently.
- `PolicyBroker::decide`, `Tool::call`, `ToolRegistry::find`, and `SessionService::call_tool` use the same signatures throughout the plan.
- The `ask_with_tool` path sends `std::vector<Evidence>` to `ModelProvider::complete` through `ModelRequest`.
