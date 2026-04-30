#include <kasli/app/default_paths.hpp>

#include <cstdlib>
#include <string>

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

}  // namespace

namespace detail {

std::filesystem::path default_socket_path_from_env(const EnvLookup& env) {
  if (const auto runtime_dir = env("XDG_RUNTIME_DIR")) {
    return std::filesystem::path(*runtime_dir) / "kaslid.sock";
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
