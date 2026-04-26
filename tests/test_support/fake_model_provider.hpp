#pragma once

#include <kasli/model/model_provider.hpp>

namespace kasli::test_support {

class FakeModelProvider final : public model::ModelProvider {
 public:
  std::string complete(const model::ModelRequest& request) const override {
    ++calls;
    return "Fake answer based on " + std::to_string(request.evidence.size()) +
           " evidence record(s).";
  }

  mutable int calls = 0;
};

}  // namespace kasli::test_support
