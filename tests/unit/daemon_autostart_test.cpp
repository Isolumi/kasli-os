#include <catch2/catch_test_macros.hpp>
#include <kasli/app/daemon_autostart.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

kasli::app::DaemonAutostartDeps default_test_deps() {
  return kasli::app::DaemonAutostartDeps{
      .request = [](const std::filesystem::path&, const std::string&) { return std::string{"ok"}; },
      .start_user_service = [] { return 0; },
      .socket_exists = [](const std::filesystem::path&) { return true; },
      .sleep_for = [](std::chrono::milliseconds) {},
  };
}

template <typename Action>
void require_throws_connect_error(Action action, int expected_error_number) {
  try {
    action();
    FAIL("expected exception");
  } catch (const kasli::ipc::UnixSocketConnectError& error) {
    REQUIRE(error.error_number() == expected_error_number);
  }
}

}  // namespace

TEST_CASE("missing default socket starts the service and retries") {
  auto deps = default_test_deps();
  int request_count = 0;
  int start_count = 0;

  deps.request = [&](const std::filesystem::path& socket_path, const std::string& request) {
    REQUIRE(socket_path == std::filesystem::path("/run/user/1000/kaslid.sock"));
    REQUIRE(request == "ping");
    ++request_count;
    if (request_count == 1) {
      throw kasli::ipc::UnixSocketConnectError(ENOENT);
    }
    return std::string{"pong"};
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };
  deps.socket_exists = [](const std::filesystem::path&) { return true; };

  const auto response = kasli::app::request_with_optional_user_service_start(
      "/run/user/1000/kaslid.sock", "ping", true, deps);

  REQUIRE(response == "pong");
  REQUIRE(request_count == 2);
  REQUIRE(start_count == 1);
}

TEST_CASE("stale default socket starts the service and retries") {
  auto deps = default_test_deps();
  int request_count = 0;
  int start_count = 0;

  deps.request = [&](const std::filesystem::path& socket_path, const std::string& request) {
    REQUIRE(socket_path == std::filesystem::path("/run/user/1000/kaslid.sock"));
    REQUIRE(request == "ping");
    ++request_count;
    if (request_count == 1) {
      throw kasli::ipc::UnixSocketConnectError(ECONNREFUSED);
    }
    return std::string{"pong"};
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };
  deps.socket_exists = [](const std::filesystem::path&) { return true; };

  const auto response = kasli::app::request_with_optional_user_service_start(
      "/run/user/1000/kaslid.sock", "ping", true, deps);

  REQUIRE(response == "pong");
  REQUIRE(request_count == 2);
  REQUIRE(start_count == 1);
}

TEST_CASE("explicit socket never starts the service") {
  auto deps = default_test_deps();
  int start_count = 0;
  deps.request = [](const std::filesystem::path&, const std::string&) -> std::string {
    throw kasli::ipc::UnixSocketConnectError(ENOENT);
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };

  require_throws_connect_error(
      [&] {
        (void)kasli::app::request_with_optional_user_service_start("/tmp/kaslid.sock", "ping",
                                                                   false, deps);
      },
      ENOENT);
  REQUIRE(start_count == 0);
}

TEST_CASE("explicit stale socket never starts the service") {
  auto deps = default_test_deps();
  int start_count = 0;
  deps.request = [](const std::filesystem::path&, const std::string&) -> std::string {
    throw kasli::ipc::UnixSocketConnectError(ECONNREFUSED);
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };

  require_throws_connect_error(
      [&] {
        (void)kasli::app::request_with_optional_user_service_start("/tmp/kaslid.sock", "ping",
                                                                   false, deps);
      },
      ECONNREFUSED);
  REQUIRE(start_count == 0);
}

TEST_CASE("start failure reports systemd status command") {
  auto deps = default_test_deps();
  deps.request = [](const std::filesystem::path&, const std::string&) -> std::string {
    throw kasli::ipc::UnixSocketConnectError(ENOENT);
  };
  deps.start_user_service = [] { return 1; };

  try {
    (void)kasli::app::request_with_optional_user_service_start("/run/user/1000/kaslid.sock", "ping",
                                                               true, deps);
    FAIL("start failure should throw");
  } catch (const std::exception& error) {
    REQUIRE(std::string(error.what()).find("systemctl --user status kaslid") != std::string::npos);
  }
}

TEST_CASE("missing socket after start reports systemd status command") {
  auto deps = default_test_deps();
  int request_count = 0;
  std::vector<std::chrono::milliseconds> sleeps;
  deps.request = [&](const std::filesystem::path&, const std::string&) -> std::string {
    ++request_count;
    throw kasli::ipc::UnixSocketConnectError(ENOENT);
  };
  deps.socket_exists = [](const std::filesystem::path&) { return false; };
  deps.sleep_for = [&](std::chrono::milliseconds duration) { sleeps.push_back(duration); };

  try {
    (void)kasli::app::request_with_optional_user_service_start("/run/user/1000/kaslid.sock", "ping",
                                                               true, deps);
    FAIL("missing socket after start should throw");
  } catch (const std::exception& error) {
    REQUIRE(std::string(error.what()).find("systemctl --user status kaslid") != std::string::npos);
  }
  REQUIRE(request_count == 1);
  auto total_sleep = std::chrono::milliseconds::zero();
  for (const auto sleep : sleeps) {
    total_sleep += sleep;
  }
  REQUIRE(total_sleep == std::chrono::milliseconds(2000));
}

TEST_CASE("socket readiness retries connection refused after service start") {
  auto deps = default_test_deps();
  int request_count = 0;
  int start_count = 0;
  std::vector<std::chrono::milliseconds> sleeps;

  deps.request = [&](const std::filesystem::path&, const std::string&) -> std::string {
    ++request_count;
    if (request_count == 1) {
      throw kasli::ipc::UnixSocketConnectError(ENOENT);
    }
    if (request_count == 2) {
      throw kasli::ipc::UnixSocketConnectError(ECONNREFUSED);
    }
    return "pong";
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };
  deps.socket_exists = [](const std::filesystem::path&) { return true; };
  deps.sleep_for = [&](std::chrono::milliseconds duration) { sleeps.push_back(duration); };

  const auto response = kasli::app::request_with_optional_user_service_start(
      "/run/user/1000/kaslid.sock", "ping", true, deps);

  REQUIRE(response == "pong");
  REQUIRE(request_count == 3);
  REQUIRE(start_count == 1);
  REQUIRE(sleeps == std::vector<std::chrono::milliseconds>{std::chrono::milliseconds(100)});
}

TEST_CASE("non-missing connection errors are returned without starting the service") {
  auto deps = default_test_deps();
  int start_count = 0;
  deps.request = [](const std::filesystem::path&, const std::string&) -> std::string {
    throw kasli::ipc::UnixSocketConnectError(EACCES);
  };
  deps.start_user_service = [&] {
    ++start_count;
    return 0;
  };

  require_throws_connect_error(
      [&] {
        (void)kasli::app::request_with_optional_user_service_start("/run/user/1000/kaslid.sock",
                                                                   "ping", true, deps);
      },
      EACCES);
  REQUIRE(start_count == 0);
}
