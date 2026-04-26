#include <kasli/ipc/unix_socket.hpp>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace kasli::ipc {
namespace {

std::runtime_error syscall_error(const std::string& operation) {
  return std::runtime_error(operation + " failed: " + std::strerror(errno));
}

bool is_stale_socket_connect_error(int error) {
  return error == ECONNREFUSED || error == ENOENT || error == ENOTSOCK
#ifdef EPROTOTYPE
         || error == EPROTOTYPE
#endif
      ;
}

sockaddr_un make_address(const std::filesystem::path& socket_path) {
  const std::string path = socket_path.string();
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    throw std::runtime_error("unix socket path is too long: " + path);
  }
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  return address;
}

void close_fd(int fd) noexcept {
  if (fd >= 0) {
    while (::close(fd) != 0 && errno == EINTR) {
    }
  }
}

void set_socket_timeouts(int fd) {
  timeval timeout{};
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;

  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
    throw syscall_error("setsockopt SO_RCVTIMEO");
  }
  if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
    throw syscall_error("setsockopt SO_SNDTIMEO");
  }
}

void suppress_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
    throw syscall_error("setsockopt SO_NOSIGPIPE");
  }
#else
  (void)fd;
#endif
}

ssize_t send_without_sigpipe(int fd, const char* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
  return ::send(fd, data, size, MSG_NOSIGNAL);
#else
  return ::send(fd, data, size, 0);
#endif
}

void write_all(int fd, const std::string& data) {
  const char* current = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
    const ssize_t written = send_without_sigpipe(fd, current, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw syscall_error("write");
    }
    if (written == 0) {
      throw std::runtime_error("write failed: wrote zero bytes");
    }
    current += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

std::string read_all(int fd) {
  std::string data;
  char buffer[4096];
  while (true) {
    const ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer));
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw syscall_error("read");
    }
    if (bytes_read == 0) {
      return data;
    }
    data.append(buffer, static_cast<std::size_t>(bytes_read));
  }
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) : fd_(fd) {}
  ~FileDescriptor() { close_fd(fd_); }

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  int get() const noexcept { return fd_; }

 private:
  int fd_;
};

void prepare_connected_socket(int fd) {
  set_socket_timeouts(fd);
  suppress_sigpipe(fd);
}

void reject_active_socket_or_remove_stale(const std::filesystem::path& socket_path) {
  if (!std::filesystem::exists(socket_path)) {
    return;
  }

  FileDescriptor probe(::socket(AF_UNIX, SOCK_STREAM, 0));
  if (probe.get() < 0) {
    throw syscall_error("socket");
  }

  const sockaddr_un address = make_address(socket_path);
  if (::connect(probe.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
    throw std::runtime_error("socket path is already in use: " + socket_path.string());
  }

  const int connect_error = errno;
  if (!is_stale_socket_connect_error(connect_error)) {
    throw std::runtime_error("socket path probe failed: " + socket_path.string() + ": " +
                             std::strerror(connect_error));
  }

  std::filesystem::remove(socket_path);
}

}  // namespace

UnixSocketServer::UnixSocketServer(std::filesystem::path socket_path)
    : socket_path_(std::move(socket_path)) {
  if (socket_path_.has_parent_path()) {
    std::filesystem::create_directories(socket_path_.parent_path());
  }

  reject_active_socket_or_remove_stale(socket_path_);

  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) {
    throw syscall_error("socket");
  }

  try {
    set_socket_timeouts(fd_);
    suppress_sigpipe(fd_);
    const sockaddr_un address = make_address(socket_path_);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      throw syscall_error("bind");
    }
    owns_socket_path_ = true;
    if (::listen(fd_, 16) != 0) {
      throw syscall_error("listen");
    }
  } catch (...) {
    close_fd(fd_);
    fd_ = -1;
    if (owns_socket_path_) {
      std::filesystem::remove(socket_path_);
      owns_socket_path_ = false;
    }
    throw;
  }
}

UnixSocketServer::~UnixSocketServer() {
  close_fd(fd_);
  if (owns_socket_path_) {
    std::error_code ignored;
    std::filesystem::remove(socket_path_, ignored);
  }
}

void UnixSocketServer::accept_one(
    const std::function<std::string(const std::string&)>& handler) const {
  int client_fd = -1;
  while (client_fd < 0) {
    client_fd = ::accept(fd_, nullptr, nullptr);
    if (client_fd < 0 && errno != EINTR) {
      throw syscall_error("accept");
    }
  }

  FileDescriptor client(client_fd);
  prepare_connected_socket(client.get());
  const std::string request = read_all(client.get());
  const std::string response = handler(request);
  write_all(client.get(), response);
}

std::string request_over_unix_socket(const std::filesystem::path& socket_path,
                                     const std::string& request) {
  FileDescriptor fd(::socket(AF_UNIX, SOCK_STREAM, 0));
  if (fd.get() < 0) {
    throw syscall_error("socket");
  }
  prepare_connected_socket(fd.get());

  const sockaddr_un address = make_address(socket_path);
  if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    throw syscall_error("connect");
  }

  write_all(fd.get(), request);
  if (::shutdown(fd.get(), SHUT_WR) != 0) {
    throw syscall_error("shutdown");
  }
  return read_all(fd.get());
}

}  // namespace kasli::ipc
