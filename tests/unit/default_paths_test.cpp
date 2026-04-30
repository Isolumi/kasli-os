#include <catch2/catch_test_macros.hpp>
#include <kasli/app/default_paths.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

kasli::app::detail::EnvLookup env_lookup(std::map<std::string, std::string> values) {
  return [values = std::move(values)](std::string_view key) -> std::optional<std::string> {
    const auto found = values.find(std::string(key));
    if (found == values.end()) {
      return std::nullopt;
    }
    return found->second;
  };
}

}  // namespace

TEST_CASE("default socket path uses XDG runtime directory") {
  const auto path = kasli::app::detail::default_socket_path_from_env(
      env_lookup({{"XDG_RUNTIME_DIR", "/run/user/1000"}}));

  REQUIRE(path == std::filesystem::path("/run/user/1000/kaslid.sock"));
}

TEST_CASE("default socket path falls back to development socket") {
  const auto path = kasli::app::detail::default_socket_path_from_env(env_lookup({}));

  REQUIRE(path == std::filesystem::path("kaslid.sock"));
}

TEST_CASE("default audit log path uses XDG state home") {
  const auto path = kasli::app::detail::default_audit_log_path_from_env(
      env_lookup({{"XDG_STATE_HOME", "/home/example/.local/state"}}));

  REQUIRE(path == std::filesystem::path("/home/example/.local/state/kasli/audit.jsonl"));
}

TEST_CASE("default audit log path falls back to home local state") {
  const auto path = kasli::app::detail::default_audit_log_path_from_env(
      env_lookup({{"HOME", "/home/example"}}));

  REQUIRE(path == std::filesystem::path("/home/example/.local/state/kasli/audit.jsonl"));
}

TEST_CASE("default audit log path falls back to development audit log") {
  const auto path = kasli::app::detail::default_audit_log_path_from_env(env_lookup({}));

  REQUIRE(path == std::filesystem::path("kasli-audit.jsonl"));
}
