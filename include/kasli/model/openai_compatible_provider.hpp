#pragma once

#include <kasli/model/model_provider.hpp>

#include <string>

namespace kasli::model {

std::string build_openai_chat_completions_payload_for_test(const ModelRequest& request,
                                                           const std::string& model);
std::string parse_openai_chat_completions_response_for_test(const std::string& body);
std::string openai_chat_completions_endpoint_for_test(const std::string& endpoint);

class OpenAICompatibleProvider final : public ModelProvider {
 public:
  OpenAICompatibleProvider(std::string endpoint, std::string model);

  std::string complete(const ModelRequest& request) const override;

 private:
  std::string endpoint_;
  std::string model_;
};

}  // namespace kasli::model
