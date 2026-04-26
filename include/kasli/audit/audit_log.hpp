#pragma once

#include <filesystem>
#include <kasli/core/types.hpp>

namespace kasli::audit {

class AuditLog {
 public:
  explicit AuditLog(std::filesystem::path path);

  void append(const core::AuditEvent& event) const;
  const std::filesystem::path& path() const noexcept;

 private:
  std::filesystem::path path_;
};

}  // namespace kasli::audit
