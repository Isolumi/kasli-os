#include <catch2/catch_test_macros.hpp>
#include <kasli/audit/audit_log.hpp>
#include <kasli/core/types.hpp>

#include "../test_support/temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

kasli::core::AuditEvent event_with_id(const std::string& id) {
  return kasli::core::AuditEvent{
      .id = id,
      .timestamp = "2026-04-26T00:00:00Z",
      .actor = "tester",
      .type = "tool.call",
      .summary = "called system.info",
      .details = {{"tool", "system.info"}},
  };
}

}  // namespace

TEST_CASE("audit log appends one JSON event per line") {
  const auto dir = kasli::test_support::temp_path("audit_log_test");
  const auto path = dir / "audit.jsonl";

  kasli::audit::AuditLog log(path);
  log.append(event_with_id("event-1"));

  std::ifstream input(path);
  std::string line;
  REQUIRE(std::getline(input, line));

  auto json = nlohmann::json::parse(line);
  REQUIRE(json.at("id") == "event-1");
  REQUIRE(json.at("details").at("tool") == "system.info");
  REQUIRE_FALSE(std::getline(input, line));
}

TEST_CASE("audit log preserves prior records across multiple appends") {
  const auto dir = kasli::test_support::temp_path("audit_log_multi_test");
  const auto path = dir / "audit.jsonl";

  kasli::audit::AuditLog log(path);
  log.append(event_with_id("event-1"));
  log.append(event_with_id("event-2"));

  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }

  REQUIRE(lines.size() == 2);
  REQUIRE(nlohmann::json::parse(lines.at(0)).at("id") == "event-1");
  REQUIRE(nlohmann::json::parse(lines.at(1)).at("id") == "event-2");
}

TEST_CASE("audit log serializes concurrent appends within the process") {
  const auto dir = kasli::test_support::temp_path("audit_log_concurrent_test");
  const auto path = dir / "audit.jsonl";
  const kasli::audit::AuditLog log(path);

  constexpr int thread_count = 8;
  constexpr int appends_per_thread = 25;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
    threads.emplace_back([&log, thread_index] {
      for (int event_index = 0; event_index < appends_per_thread; ++event_index) {
        log.append(event_with_id("thread-" + std::to_string(thread_index) + "-event-" +
                                 std::to_string(event_index)));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  std::ifstream input(path);
  std::set<std::string> ids;
  std::string line;
  int line_count = 0;
  while (std::getline(input, line)) {
    const auto json = nlohmann::json::parse(line);
    REQUIRE(json.at("id").is_string());
    ids.insert(json.at("id").get<std::string>());
    ++line_count;
  }

  REQUIRE(line_count == thread_count * appends_per_thread);
  REQUIRE(ids.size() == thread_count * appends_per_thread);
}

TEST_CASE("audit log constructor creates parent directories") {
  const auto dir = kasli::test_support::temp_path("audit_log_parent_test");
  const auto path = dir / "nested" / "audit.jsonl";

  const kasli::audit::AuditLog log(path);

  REQUIRE(std::filesystem::is_directory(path.parent_path()));
  REQUIRE(log.path() == path);
}

TEST_CASE("audit log append throws when path cannot be opened for writing") {
  const auto dir = kasli::test_support::temp_path("audit_log_open_failure_test");
  const auto path = dir / "audit.jsonl";
  std::filesystem::create_directory(path);

  const kasli::audit::AuditLog log(path);

  REQUIRE_THROWS_AS(log.append(event_with_id("event-1")), std::runtime_error);
}

TEST_CASE("audit log append throws on write failure") {
  const std::filesystem::path dev_full = "/dev/full";
  if (!std::filesystem::exists(dev_full)) {
    SUCCEED("/dev/full is not available on this platform");
    return;
  }

  const kasli::audit::AuditLog log(dev_full);

  REQUIRE_THROWS_AS(log.append(event_with_id("event-1")), std::runtime_error);
}
