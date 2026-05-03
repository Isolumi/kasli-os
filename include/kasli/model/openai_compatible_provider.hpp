#pragma once

#include <kasli/model/model_provider.hpp>

#include <functional>
#include <string>

namespace kasli::model {

std::string build_openai_chat_completions_payload_for_test(const ModelRequest& request,
                                                           const std::string& model);
std::string parse_openai_chat_completions_response_for_test(const std::string& body);
std::string openai_chat_completions_endpoint_for_test(const std::string& endpoint);

struct OpenAICompatibleHttpResponse {
  long status_code = 0;
  std::string body;
};

using OpenAICompatibleHttpPost =
    std::function<OpenAICompatibleHttpResponse(const std::string& url,
                                               const std::string& payload)>;

class OpenAICompatibleProvider final : public ModelProvider {
 public:
  OpenAICompatibleProvider(std::string endpoint, std::string model);
  OpenAICompatibleProvider(std::string endpoint,
                           std::string model,
                           OpenAICompatibleHttpPost post);

  std::string complete(const ModelRequest& request) const override;

 private:
  std::string endpoint_;
  std::string model_;
  OpenAICompatibleHttpPost post_;
};

}  // namespace kasli::model
