#pragma once

#include <kasli/core/types.hpp>

#include <string>
#include <vector>

namespace kasli::model {

struct ModelRequest {
  std::string prompt;
  std::vector<core::Evidence> evidence;
};

class ModelProvider {
 public:
  virtual ~ModelProvider() = default;
  virtual std::string complete(const ModelRequest& request) const = 0;
};

}  // namespace kasli::model
