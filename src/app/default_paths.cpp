#include <kasli/app/default_paths.hpp>

#include <cstdlib>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace kasli::app {
namespace {

std::optional<std::string> getenv_value(std::string_view key) {
  const std::string name(key);
  const char* value = std::getenv(name.c_str());
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

std::optional<std::filesystem::path> linux_user_runtime_dir() {
#if defined(__linux__)
  const auto path = std::filesystem::path("/run/user") / std::to_string(::geteuid());
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    return path;
  }
#endif
  return std::nullopt;
}

}  // namespace

namespace detail {

std::filesystem::path default_socket_path_from_env(const EnvLookup& env) {
  return default_socket_path_from_env(env, linux_user_runtime_dir);
}

std::filesystem::path default_socket_path_from_env(const EnvLookup& env,
                                                   const RuntimeDirLookup& runtime_dir) {
  if (const auto runtime_dir_env = env("XDG_RUNTIME_DIR");
      runtime_dir_env && !runtime_dir_env->empty()) {
    return std::filesystem::path(*runtime_dir_env) / "kaslid.sock";
  }
  if (const auto fallback_runtime_dir = runtime_dir()) {
    return *fallback_runtime_dir / "kaslid.sock";
  }
  return "kaslid.sock";
}

std::filesystem::path default_audit_log_path_from_env(const EnvLookup& env) {
  if (const auto state_home = env("XDG_STATE_HOME")) {
    return std::filesystem::path(*state_home) / "kasli" / "audit.jsonl";
  }
  if (const auto home = env("HOME")) {
    return std::filesystem::path(*home) / ".local" / "state" / "kasli" / "audit.jsonl";
  }
  return "kasli-audit.jsonl";
}

}  // namespace detail

std::filesystem::path default_socket_path() {
  return detail::default_socket_path_from_env(getenv_value);
}

std::filesystem::path default_audit_log_path() {
  return detail::default_audit_log_path_from_env(getenv_value);
}

}  // namespace kasli::app
