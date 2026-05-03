cmake_minimum_required(VERSION 3.24)

project(kasli_os VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KASLI_ENABLE_SYSTEMD "Build systemd and journal adapters when libsystemd is available" ON)
option(KASLI_ENABLE_CURL "Build Ollama provider when libcurl is available" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KasliDependencies)
include(GNUInstallDirs)

include(CTest)
include(Catch)

add_library(kasli_core
  src/app/daemon_autostart.cpp
  src/app/default_paths.cpp
  src/audit/audit_log.cpp
  src/core/json.cpp
  src/core/types.cpp
  src/core/uuid.cpp
  src/ipc/line_protocol.cpp
  src/ipc/unix_socket.cpp
  src/model/ollama_provider.cpp
  src/policy/policy_broker.cpp
  src/session/session_service.cpp
  src/tools/disk_tool.cpp
  src/tools/hardware_tool.cpp
  src/tools/journal_tool.cpp
  src/tools/network_tool.cpp
  src/tools/package_tool.cpp
  src/tools/power_tool.cpp
  src/tools/service_diagnose_tool.cpp
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

install(TARGETS kaslid kasli
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES packaging/systemd/user/kaslid.service
  DESTINATION "lib/systemd/user"
)
install(FILES README.md PROGRESS.md
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/kasli-os"
)

add_executable(kasli_smoke_test tests/unit/smoke_test.cpp)
target_link_libraries(kasli_smoke_test PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(kasli_smoke_test)

add_executable(kasli_core_types_test tests/unit/core_types_test.cpp)
target_link_libraries(kasli_core_types_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_core_types_test TEST_PREFIX "core_types.")

add_executable(kasli_default_paths_test tests/unit/default_paths_test.cpp)
target_link_libraries(kasli_default_paths_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_default_paths_test TEST_PREFIX "app.")

add_executable(kasli_daemon_autostart_test tests/unit/daemon_autostart_test.cpp)
target_link_libraries(kasli_daemon_autostart_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_daemon_autostart_test TEST_PREFIX "app.")

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

add_executable(kasli_service_diagnose_tool_test tests/unit/service_diagnose_tool_test.cpp)
target_link_libraries(kasli_service_diagnose_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_service_diagnose_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_package_tool_test tests/unit/package_tool_test.cpp)
target_link_libraries(kasli_package_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_package_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_disk_tool_test tests/unit/disk_tool_test.cpp)
target_link_libraries(kasli_disk_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_disk_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_hardware_tool_test tests/unit/hardware_tool_test.cpp)
target_link_libraries(kasli_hardware_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_hardware_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_network_tool_test tests/unit/network_tool_test.cpp)
target_link_libraries(kasli_network_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_network_tool_test
  TEST_PREFIX "tools."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(kasli_power_tool_test tests/unit/power_tool_test.cpp)
target_link_libraries(kasli_power_tool_test PRIVATE kasli_core Catch2::Catch2WithMain)
catch_discover_tests(kasli_power_tool_test
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

set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "kasli-os")
set(CPACK_PACKAGE_VENDOR "Kasli")
set(CPACK_PACKAGE_CONTACT "Kasli Prototype Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Kasli read-only local system assistant prototype"
)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}")
set(CPACK_RPM_PACKAGE_NAME "kasli-os")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "LicenseRef-Kasli-Prototype")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_DESCRIPTION
  "Kasli is a read-only local daemon and CLI prototype that exposes typed operating-system inspection tools through a constrained, auditable interface."
)
include(CPack)
