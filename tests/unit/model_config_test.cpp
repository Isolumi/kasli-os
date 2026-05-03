#include <catch2/catch_test_macros.hpp>
#include <kasli/model/model_config.hpp>
#include <kasli/model/ollama_provider.hpp>
#include <kasli/model/openai_compatible_provider.hpp>

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

kasli::model::detail::EnvLookup env_lookup(std::map<std::string, std::string> values) {
  return [values = std::move(values)](std::string_view key) -> std::optional<std::string> {
    const auto found = values.find(std::string(key));
    if (found == values.end()) {
      return std::nullopt;
    }
    return found->second;
  };
}

}  // namespace

TEST_CASE("default model config uses ollama defaults") {
  const auto config = kasli::model::default_model_config();

  REQUIRE(config.provider == kasli::model::ModelProviderKind::Ollama);
  REQUIRE(config.endpoint == "http://127.0.0.1:11434");
  REQUIRE(config.model == "gemma4");
  REQUIRE(kasli::model::model_provider_kind_to_string(config.provider) == "ollama");
}

TEST_CASE("empty environment uses ollama defaults") {
  const auto config = kasli::model::model_config_from_env(env_lookup({}));

  REQUIRE(config.provider == kasli::model::ModelProviderKind::Ollama);
  REQUIRE(config.endpoint == "http://127.0.0.1:11434");
  REQUIRE(config.model == "gemma4");
}

TEST_CASE("openai-compatible provider environment uses local defaults") {
  const auto config = kasli::model::model_config_from_env(
      env_lookup({{"KASLI_MODEL_PROVIDER", "openai-compatible"}}));

  REQUIRE(config.provider == kasli::model::ModelProviderKind::OpenAICompatible);
  REQUIRE(config.endpoint == "http://127.0.0.1:1234/v1");
  REQUIRE(config.model == "local-model");
  REQUIRE(kasli::model::model_provider_kind_to_string(config.provider) == "openai-compatible");
}

TEST_CASE("ollama environment accepts explicit daemon endpoint and model") {
  const auto config = kasli::model::model_config_from_env(env_lookup({
      {"KASLI_MODEL_PROVIDER", "ollama"},
      {"KASLI_MODEL_ENDPOINT", "http://127.0.0.1:11434"},
      {"KASLI_MODEL_NAME", "kimi-k2"},
  }));

  REQUIRE(config.provider == kasli::model::ModelProviderKind::Ollama);
  REQUIRE(kasli::model::model_provider_kind_to_string(config.provider) == "ollama");
  REQUIRE(config.endpoint == "http://127.0.0.1:11434");
  REQUIRE(config.model == "kimi-k2");
}

TEST_CASE("endpoint and model name override ollama defaults") {
  const auto config = kasli::model::model_config_from_env(env_lookup({
      {"KASLI_MODEL_PROVIDER", "ollama"},
      {"KASLI_MODEL_ENDPOINT", "http://127.0.0.1:11435"},
      {"KASLI_MODEL_NAME", "kimi-k2"},
  }));

  REQUIRE(config.provider == kasli::model::ModelProviderKind::Ollama);
  REQUIRE(config.endpoint == "http://127.0.0.1:11435");
  REQUIRE(config.model == "kimi-k2");
}

TEST_CASE("endpoint and model name override openai-compatible defaults") {
  const auto config = kasli::model::model_config_from_env(env_lookup({
      {"KASLI_MODEL_PROVIDER", "openai-compatible"},
      {"KASLI_MODEL_ENDPOINT", "http://127.0.0.1:8080/v1/chat/completions"},
      {"KASLI_MODEL_NAME", "qwen3"},
  }));

  REQUIRE(config.provider == kasli::model::ModelProviderKind::OpenAICompatible);
  REQUIRE(config.endpoint == "http://127.0.0.1:8080/v1/chat/completions");
  REQUIRE(config.model == "qwen3");
}

TEST_CASE("unknown provider environment value throws clear error") {
  try {
    (void)kasli::model::model_config_from_env(env_lookup({{"KASLI_MODEL_PROVIDER", "remote"}}));
    FAIL("expected unknown provider to throw");
  } catch (const std::invalid_argument& error) {
    const std::string message(error.what());
    REQUIRE(message.find("unknown model provider") != std::string::npos);
    REQUIRE(message.find("remote") != std::string::npos);
  }
}

TEST_CASE("factory creates provider implementation for selected kind") {
  const auto ollama_provider = kasli::model::make_model_provider(kasli::model::ModelConfig{
      .provider = kasli::model::ModelProviderKind::Ollama,
      .endpoint = "http://127.0.0.1:11434",
      .model = "gemma4",
  });
  REQUIRE(dynamic_cast<kasli::model::OllamaProvider*>(ollama_provider.get()) != nullptr);

  const auto openai_provider = kasli::model::make_model_provider(kasli::model::ModelConfig{
      .provider = kasli::model::ModelProviderKind::OpenAICompatible,
      .endpoint = "http://127.0.0.1:1234/v1",
      .model = "local-model",
  });
  REQUIRE(dynamic_cast<kasli::model::OpenAICompatibleProvider*>(openai_provider.get()) !=
          nullptr);
}
