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
