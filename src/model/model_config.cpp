#include <kasli/model/model_config.hpp>

#include <kasli/model/ollama_provider.hpp>
#include <kasli/model/openai_compatible_provider.hpp>

#include <stdexcept>
#include <string>

namespace kasli::model {
namespace {

constexpr auto kOllamaEndpoint = "http://127.0.0.1:11434";
constexpr auto kOllamaModel = "gemma4";
constexpr auto kOpenAICompatibleEndpoint = "http://127.0.0.1:1234/v1";
constexpr auto kOpenAICompatibleModel = "local-model";

bool has_value(const std::optional<std::string>& value) {
  return value && !value->empty();
}

ModelProviderKind provider_from_name(const std::string& name) {
  if (name == "ollama") {
    return ModelProviderKind::Ollama;
  }
  if (name == "openai-compatible") {
    return ModelProviderKind::OpenAICompatible;
  }
  throw std::invalid_argument("unknown model provider '" + name +
                              "' (expected 'ollama' or 'openai-compatible')");
}

}  // namespace

ModelConfig default_model_config() {
  return ModelConfig{
      .provider = ModelProviderKind::Ollama,
      .endpoint = kOllamaEndpoint,
      .model = kOllamaModel,
  };
}

ModelConfig model_config_from_env(const detail::EnvLookup& env) {
  auto config = default_model_config();

  if (const auto provider = env("KASLI_MODEL_PROVIDER"); has_value(provider)) {
    config.provider = provider_from_name(*provider);
    if (config.provider == ModelProviderKind::OpenAICompatible) {
      config.endpoint = kOpenAICompatibleEndpoint;
      config.model = kOpenAICompatibleModel;
    }
  }

  if (const auto endpoint = env("KASLI_MODEL_ENDPOINT"); has_value(endpoint)) {
    config.endpoint = *endpoint;
  }
  if (const auto model = env("KASLI_MODEL_NAME"); has_value(model)) {
    config.model = *model;
  }

  return config;
}

std::unique_ptr<ModelProvider> make_model_provider(const ModelConfig& config) {
  switch (config.provider) {
    case ModelProviderKind::Ollama:
      return std::make_unique<OllamaProvider>(config.endpoint, config.model);
    case ModelProviderKind::OpenAICompatible:
      return std::make_unique<OpenAICompatibleProvider>(config.endpoint, config.model);
  }
  throw std::invalid_argument("unknown model provider kind");
}

std::string model_provider_kind_to_string(ModelProviderKind provider) {
  switch (provider) {
    case ModelProviderKind::Ollama:
      return "ollama";
    case ModelProviderKind::OpenAICompatible:
      return "openai-compatible";
  }
  throw std::invalid_argument("unknown model provider kind");
}

}  // namespace kasli::model
