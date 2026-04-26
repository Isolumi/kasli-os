#pragma once

#include <kasli/model/model_provider.hpp>

#include <string>

namespace kasli::model {

std::string build_ollama_prompt_for_test(const ModelRequest& request);
std::string parse_ollama_response_for_test(const std::string& body);

class OllamaProvider final : public ModelProvider {
 public:
  OllamaProvider(std::string endpoint, std::string model);

  std::string complete(const ModelRequest& request) const override;

 private:
  std::string endpoint_;
  std::string model_;
};

}  // namespace kasli::model
