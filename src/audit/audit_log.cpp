#include <kasli/audit/audit_log.hpp>

#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace kasli::audit {
namespace {

std::mutex audit_log_mutex;

std::runtime_error write_error(const std::filesystem::path& path) {
  return std::runtime_error("failed to write audit log: " + path.string());
}

}  // namespace

AuditLog::AuditLog(std::filesystem::path path) : path_(std::move(path)) {
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path());
  }
}

void AuditLog::append(const core::AuditEvent& event) const {
  const nlohmann::json encoded = event;
  const std::string record = encoded.dump() + '\n';

  const std::lock_guard lock(audit_log_mutex);
  std::ofstream output(path_, std::ios::app);
  if (!output) {
    throw std::runtime_error("failed to open audit log: " + path_.string());
  }

  output << record;
  if (!output) {
    throw write_error(path_);
  }

  output.flush();
  if (!output) {
    throw write_error(path_);
  }

  output.close();
  if (!output) {
    throw write_error(path_);
  }
}

const std::filesystem::path& AuditLog::path() const noexcept {
  return path_;
}

}  // namespace kasli::audit
