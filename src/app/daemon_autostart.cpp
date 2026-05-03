#include <kasli/app/daemon_autostart.hpp>

#include <kasli/ipc/unix_socket.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace kasli::app {
namespace {

constexpr auto kStartCommand = "systemctl --user start kaslid";
constexpr auto kStatusCommand = "systemctl --user status kaslid";

bool is_missing_socket_error(const kasli::ipc::UnixSocketConnectError& error) {
  return error.error_number() == ENOENT;
}

bool is_socket_not_ready_error(const kasli::ipc::UnixSocketConnectError& error) {
  return error.error_number() == ENOENT || error.error_number() == ECONNREFUSED;
}

std::runtime_error daemon_start_error(const std::string& reason) {
  return std::runtime_error(reason + ". Inspect daemon status with: " + kStatusCommand);
}

std::string request_after_start(const std::filesystem::path& socket_path,
                                const std::string& request,
                                const DaemonAutostartDeps& deps) {
  using namespace std::chrono_literals;

  constexpr auto max_wait = 2000ms;
  constexpr auto poll_interval = 100ms;
  auto waited = 0ms;
  std::string last_error = "socket path did not appear";

  while (waited <= max_wait) {
    if (deps.socket_exists(socket_path)) {
      try {
        return deps.request(socket_path, request);
      } catch (const kasli::ipc::UnixSocketConnectError& error) {
        if (!is_socket_not_ready_error(error)) {
          throw;
        }
        last_error = error.what();
      }
    }
    if (waited == max_wait) {
      break;
    }

    const auto remaining = max_wait - waited;
    const auto sleep_duration = std::min(poll_interval, remaining);
    deps.sleep_for(sleep_duration);
    waited += sleep_duration;
  }

  throw daemon_start_error("kaslid socket was not ready at " + socket_path.string() + " after `" +
                           kStartCommand + "`: " + last_error);
}

}  // namespace

DaemonAutostartDeps default_daemon_autostart_deps() {
  return DaemonAutostartDeps{
      .request = kasli::ipc::request_over_unix_socket,
      .start_user_service = [] { return std::system(kStartCommand); },
      .socket_exists =
          [](const std::filesystem::path& socket_path) {
            std::error_code error;
            return std::filesystem::exists(socket_path, error);
          },
      .sleep_for =
          [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); },
  };
}

std::string request_with_optional_user_service_start(const std::filesystem::path& socket_path,
                                                     const std::string& request,
                                                     bool allow_autostart,
                                                     const DaemonAutostartDeps& deps) {
  try {
    return deps.request(socket_path, request);
  } catch (const kasli::ipc::UnixSocketConnectError& error) {
    const std::string first_error = error.what();
    if (!allow_autostart || !is_missing_socket_error(error)) {
      throw;
    }

    const int start_result = deps.start_user_service();
    if (start_result != 0) {
      throw daemon_start_error(std::string("failed to run `") + kStartCommand +
                               "` after initial request error: " + first_error);
    }

    return request_after_start(socket_path, request, deps);
  }
}

std::string request_with_optional_user_service_start(const std::filesystem::path& socket_path,
                                                     const std::string& request,
                                                     bool allow_autostart) {
#if defined(__linux__)
  return request_with_optional_user_service_start(socket_path, request, allow_autostart,
                                                  default_daemon_autostart_deps());
#else
  return request_with_optional_user_service_start(socket_path, request, false,
                                                  default_daemon_autostart_deps());
#endif
}

}  // namespace kasli::app
