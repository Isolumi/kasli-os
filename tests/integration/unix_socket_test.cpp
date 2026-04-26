#include <catch2/catch_test_macros.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include "../test_support/temp_dir.hpp"

#include <fstream>
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

TEST_CASE("unix socket server rejects a path already used by a live server") {
  const auto dir = kasli::test_support::temp_path("unix_socket_duplicate_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);

  REQUIRE_THROWS(kasli::ipc::UnixSocketServer(socket_path));
}

TEST_CASE("unix socket server replaces stale path") {
  const auto dir = kasli::test_support::temp_path("unix_socket_stale_test");
  const auto socket_path = dir / "kaslid.sock";
  {
    std::ofstream stale(socket_path);
    stale << "stale";
  }

  kasli::ipc::UnixSocketServer server(socket_path);
  std::thread server_thread([&] {
    server.accept_one([](const std::string& request) {
      return std::string{"fresh:"} + request;
    });
  });

  const auto response = kasli::ipc::request_over_unix_socket(socket_path, "ping");
  server_thread.join();

  REQUIRE(response == "fresh:ping");
}
