#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace kasli::app {

struct DaemonAutostartDeps {
  std::function<std::string(const std::filesystem::path&, const std::string&)> request;
  std::function<int()> start_user_service;
  std::function<bool(const std::filesystem::path&)> socket_exists;
  std::function<void(std::chrono::milliseconds)> sleep_for;
};

DaemonAutostartDeps default_daemon_autostart_deps();

std::string request_with_optional_user_service_start(const std::filesystem::path& socket_path,
                                                     const std::string& request,
                                                     bool allow_autostart,
                                                     const DaemonAutostartDeps& deps);

std::string request_with_optional_user_service_start(const std::filesystem::path& socket_path,
                                                     const std::string& request,
                                                     bool allow_autostart);

}  // namespace kasli::app
