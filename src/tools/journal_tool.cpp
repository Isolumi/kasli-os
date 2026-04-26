#include <kasli/tools/journal_tool.hpp>

#include <kasli/core/json.hpp>

#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#if KASLI_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#endif

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxEntries = 50;
constexpr std::size_t kMaxBodyBytes = 16 * 1024;

std::string json_string_value(const nlohmann::json& entry, const char* key) {
  const auto iter = entry.find(key);
  if (iter == entry.end() || !iter->is_string()) {
    return "";
  }
  return iter->get<std::string>();
}

void append_bounded(std::string& body, const std::string& text, bool& truncated) {
  if (body.size() + text.size() <= kMaxBodyBytes) {
    body += text;
    return;
  }

  const std::string marker = "truncated=true\n";
  const auto remaining = kMaxBodyBytes - body.size();
  if (remaining > marker.size()) {
    body += text.substr(0, remaining - marker.size());
  } else if (body.size() + marker.size() > kMaxBodyBytes) {
    body.resize(kMaxBodyBytes - marker.size());
  }
  body += marker;
  truncated = true;
}

void append_truncation_marker(std::string& body, bool& truncated) {
  if (!truncated) {
    append_bounded(body, "truncated=true\n", truncated);
    truncated = true;
  }
}

core::ToolResponse journal_unavailable_response(const core::ToolRequest& request) {
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "journal support was not built",
      .evidence = {},
  };
}

#if KASLI_HAS_SYSTEMD
struct JournalHandle {
  sd_journal* journal = nullptr;
  ~JournalHandle() {
    if (journal != nullptr) {
      sd_journal_close(journal);
    }
  }
};
#endif

}  // namespace

JournalFixtureTool::JournalFixtureTool(std::filesystem::path fixture_path)
    : fixture_path_(std::move(fixture_path)) {}

std::string JournalFixtureTool::name() const {
  return "journal.query";
}

core::RiskClass JournalFixtureTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse JournalFixtureTool::call(const core::ToolRequest& request) const {
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";
  std::ifstream input(fixture_path_);
  if (!input) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to open journal fixture: " + fixture_path_.string(),
        .evidence = {},
    };
  }

  std::string body;
  std::size_t entries = 0;
  std::size_t skipped_invalid_entries = 0;
  bool truncated = false;

  std::string line;
  while (std::getline(input, line)) {
    auto entry = nlohmann::json::parse(line, nullptr, false);
    if (entry.is_discarded() || !entry.is_object()) {
      ++skipped_invalid_entries;
      continue;
    }

    if (!unit.empty() && json_string_value(entry, "_SYSTEMD_UNIT") != unit) {
      continue;
    }

    if (entries >= kMaxEntries) {
      append_truncation_marker(body, truncated);
      break;
    }

    const auto message = core::redact_likely_secret(json_string_value(entry, "MESSAGE"));
    append_bounded(body, json_string_value(entry, "PRIORITY") + " " + message + '\n', truncated);
    if (truncated) {
      break;
    }
    ++entries;
  }

  if (skipped_invalid_entries > 0 && !truncated) {
    append_bounded(body,
                   "skipped_invalid_entries=" + std::to_string(skipped_invalid_entries) + '\n',
                   truncated);
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "journal fixture queried",
      .evidence = {core::Evidence{
          .id = request.id + ":journal.query",
          .source = "journal.query",
          .summary = "bounded journal entries for " + unit,
          .body = body,
          .timestamp = "",
      }},
  };
}

std::string LiveJournalTool::name() const {
  return "journal.query";
}

core::RiskClass LiveJournalTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse LiveJournalTool::call(const core::ToolRequest& request) const {
#if KASLI_HAS_SYSTEMD
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";

  JournalHandle handle;
  int result = sd_journal_open(&handle.journal, SD_JOURNAL_LOCAL_ONLY);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to open journal",
        .evidence = {},
    };
  }

  if (!unit.empty()) {
    const std::string match = "_SYSTEMD_UNIT=" + unit;
    result = sd_journal_add_match(handle.journal, match.c_str(), 0);
    if (result < 0) {
      return core::ToolResponse{
          .request_id = request.id,
          .status = core::ToolStatus::Error,
          .message = "failed to add journal unit match",
          .evidence = {},
      };
    }
  }

  result = sd_journal_seek_tail(handle.journal);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to seek journal",
        .evidence = {},
    };
  }

  std::string body;
  std::size_t entries = 0;
  bool truncated = false;

  while ((result = sd_journal_previous(handle.journal)) > 0) {
    if (entries >= kMaxEntries) {
      append_truncation_marker(body, truncated);
      break;
    }

    const void* priority_data = nullptr;
    std::size_t priority_length = 0;
    std::string priority;
    if (sd_journal_get_data(handle.journal, "PRIORITY", &priority_data, &priority_length) == 0) {
      const std::string priority_field(static_cast<const char*>(priority_data), priority_length);
      constexpr std::string_view prefix = "PRIORITY=";
      if (priority_field.starts_with(prefix)) {
        priority = priority_field.substr(prefix.size());
      }
    }

    const void* message_data = nullptr;
    std::size_t message_length = 0;
    std::string message;
    if (sd_journal_get_data(handle.journal, "MESSAGE", &message_data, &message_length) == 0) {
      const std::string message_field(static_cast<const char*>(message_data), message_length);
      constexpr std::string_view prefix = "MESSAGE=";
      if (message_field.starts_with(prefix)) {
        message = message_field.substr(prefix.size());
      }
    }

    append_bounded(body, priority + " " + core::redact_likely_secret(message) + '\n', truncated);
    if (truncated) {
      break;
    }
    ++entries;
  }

  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to read journal",
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "journal queried",
      .evidence = {core::Evidence{
          .id = request.id + ":journal.query",
          .source = "journal.query",
          .summary = "bounded recent journal entries for " + unit,
          .body = body,
          .timestamp = "",
      }},
  };
#else
  return journal_unavailable_response(request);
#endif
}

}  // namespace kasli::tools
