#pragma once

#include <kasli/model/model_provider.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kasli::model {

namespace detail {

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

}  // namespace detail

enum class ModelProviderKind { Ollama, OpenAICompatible };

struct ModelConfig {
  ModelProviderKind provider;
  std::string endpoint;
  std::string model;
};

ModelConfig default_model_config();
ModelConfig model_config_from_env(const detail::EnvLookup& env);
std::unique_ptr<ModelProvider> make_model_provider(const ModelConfig& config);
std::string model_provider_kind_to_string(ModelProviderKind provider);

}  // namespace kasli::model
