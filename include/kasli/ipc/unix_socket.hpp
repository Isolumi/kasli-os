#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace kasli::ipc {

inline constexpr std::size_t kMaxUnixSocketLineBytes = 1024 * 1024;

class UnixSocketConnectError final : public std::runtime_error {
 public:
  explicit UnixSocketConnectError(int error_number);

  int error_number() const noexcept {
    return error_number_;
  }

 private:
  int error_number_;
};

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
  bool owns_socket_path_ = false;
};

std::string request_over_unix_socket(const std::filesystem::path& socket_path,
                                     const std::string& request);

}  // namespace kasli::ipc
