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
  src/tools/systemd_tool.cpp
  src/tools/system_info_tool.cpp
  src/tools/tool_registry.cpp
)
target_include_directories(kasli_core PUBLIC include)
target_link_libraries(kasli_core PUBLIC nlohmann_json::nlohmann_json)

if(KASLI_ENABLE_CURL AND CURL_FOUND)
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_CURL=1)
  target_link_libraries(kasli_core PRIVATE CURL::libcurl)
else()
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_CURL=0)
endif()

if(LIBSYSTEMD_FOUND AND KASLI_ENABLE_SYSTEMD)
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_SYSTEMD=1)
  target_link_libraries(kasli_core PUBLIC PkgConfig::LIBSYSTEMD)
else()
  target_compile_definitions(kasli_core PUBLIC KASLI_HAS_SYSTEMD=0)
endif()

add_executable(kaslid src/kaslid/main.cpp)
target_link_libraries(kaslid PRIVATE kasli_core)
target_compile_definitions(kaslid
  PRIVATE
    KASLI_DEFAULT_JOURNAL_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/journal/ssh_failed.jsonl"
)

add_executable(kasli src/kasli-cli/main.cpp)
target_link_libraries(kasli PRIVATE kasli_core CLI11::CLI11)

add_executable(kasli_smoke_test tests/unit/smoke_test.cpp)
target_link_libraries(kasli_smoke_test PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(kasli_smoke_test)

add_executable(kasli_core_types_test tests/unit/core_types_test.cpp)
target_link_libraries(kasli_core_types_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_core_types_test TEST_PREFIX "core_types.")

add_executable(kasli_audit_log_test tests/unit/audit_log_test.cpp)
target_link_libraries(kasli_audit_log_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_audit_log_test TEST_PREFIX "audit.")

add_executable(kasli_policy_broker_test tests/unit/policy_broker_test.cpp)
target_link_libraries(kasli_policy_broker_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_policy_broker_test TEST_PREFIX "policy.")

add_executable(kasli_tool_registry_test tests/unit/tool_registry_test.cpp)
target_link_libraries(kasli_tool_registry_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_tool_registry_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_system_info_tool_test tests/unit/system_info_tool_test.cpp)
target_link_libraries(kasli_system_info_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_system_info_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_journal_tool_test tests/unit/journal_tool_test.cpp)
target_link_libraries(kasli_journal_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_journal_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_systemd_tool_test tests/unit/systemd_tool_test.cpp)
target_link_libraries(kasli_systemd_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_systemd_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_session_service_test tests/unit/session_service_test.cpp)
target_link_libraries(kasli_session_service_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_session_service_test TEST_PREFIX "session.")

add_executable(kasli_model_context_test tests/unit/model_context_test.cpp)
target_link_libraries(kasli_model_context_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_model_context_test TEST_PREFIX "model.")

add_executable(kasli_ollama_provider_test tests/unit/ollama_provider_test.cpp)
target_link_libraries(kasli_ollama_provider_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_ollama_provider_test TEST_PREFIX "model.")

add_executable(kasli_line_protocol_test tests/unit/line_protocol_test.cpp)
target_link_libraries(kasli_line_protocol_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_line_protocol_test TEST_PREFIX "ipc.")

add_executable(kasli_unix_socket_test tests/integration/unix_socket_test.cpp)
target_link_libraries(kasli_unix_socket_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_unix_socket_test TEST_PREFIX "ipc.")
