#pragma once

#include <filesystem>
#include <string>

namespace kasli::test_support {

inline std::filesystem::path temp_path(const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / ("kasli_" + name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

}  // namespace kasli::test_support
