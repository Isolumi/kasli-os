#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace kasli::app {

namespace detail {

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

std::filesystem::path default_socket_path_from_env(const EnvLookup& env);
std::filesystem::path default_audit_log_path_from_env(const EnvLookup& env);

}  // namespace detail

std::filesystem::path default_socket_path();
std::filesystem::path default_audit_log_path();

}  // namespace kasli::app
