#include <catch2/catch_test_macros.hpp>
#include <kasli/ipc/unix_socket.hpp>

#include "../test_support/temp_dir.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

sockaddr_un make_address(const std::filesystem::path& socket_path) {
  const auto path = socket_path.string();
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    throw std::runtime_error("socket path too long");
  }
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  return address;
}

void close_fd(int fd) {
  while (fd >= 0 && ::close(fd) != 0 && errno == EINTR) {
  }
}

int send_raw_request(const std::filesystem::path& socket_path, const std::string& request) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  REQUIRE(fd >= 0);
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  REQUIRE(::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0);
#endif

  const auto address = make_address(socket_path);
  REQUIRE(::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

  const char* current = request.data();
  std::size_t remaining = request.size();
  while (remaining > 0) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const ssize_t written = ::send(fd, current, remaining, flags);
    if (written < 0) {
      break;
    }
    current += written;
    remaining -= static_cast<std::size_t>(written);
  }

  ::shutdown(fd, SHUT_WR);
  return fd;
}

std::string exception_message(std::exception_ptr error) {
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& exception) {
    return exception.what();
  } catch (...) {
    return "unknown exception";
  }
}

}  // namespace

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

TEST_CASE("unix socket path is restricted to the owner") {
  const auto dir = kasli::test_support::temp_path("unix_socket_mode_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);

  struct stat socket_stat {};
  REQUIRE(::stat(socket_path.c_str(), &socket_stat) == 0);
  REQUIRE((socket_stat.st_mode & 0777) == 0600);
}

TEST_CASE("unix socket server rejects oversized requests before handler") {
  const auto dir = kasli::test_support::temp_path("unix_socket_oversized_request_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);
  std::exception_ptr server_error;
  std::thread server_thread([&] {
    try {
      server.accept_one([](const std::string&) {
        FAIL("oversized request must not reach the handler");
        return std::string{"unreachable"};
      });
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  const int client_fd =
      send_raw_request(socket_path, std::string(kasli::ipc::kMaxUnixSocketLineBytes + 1, 'x'));
  server_thread.join();
  close_fd(client_fd);

  REQUIRE(server_error != nullptr);
  REQUIRE(exception_message(server_error).find("request line is too large") != std::string::npos);
}

TEST_CASE("unix socket server rejects requests without newline framing") {
  const auto dir = kasli::test_support::temp_path("unix_socket_missing_newline_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);
  std::exception_ptr server_error;
  std::thread server_thread([&] {
    try {
      server.accept_one([](const std::string&) {
        FAIL("unframed request must not reach the handler");
        return std::string{"unreachable"};
      });
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  const int client_fd = send_raw_request(socket_path, "ping");
  server_thread.join();
  close_fd(client_fd);

  REQUIRE(server_error != nullptr);
  const auto message = exception_message(server_error);
  INFO(message);
  REQUIRE(message.find("request is missing newline framing") != std::string::npos);
}

TEST_CASE("unix socket server rejects oversized responses") {
  const auto dir = kasli::test_support::temp_path("unix_socket_oversized_response_test");
  const auto socket_path = dir / "kaslid.sock";

  kasli::ipc::UnixSocketServer server(socket_path);
  std::exception_ptr server_error;
  std::thread server_thread([&] {
    try {
      server.accept_one([](const std::string&) {
        return std::string(kasli::ipc::kMaxUnixSocketLineBytes + 1, 'x');
      });
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  REQUIRE_THROWS(kasli::ipc::request_over_unix_socket(socket_path, "ping"));
  server_thread.join();

  REQUIRE(server_error != nullptr);
  REQUIRE(exception_message(server_error).find("line is too large") != std::string::npos);
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
