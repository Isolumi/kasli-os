#include <kasli/model/openai_compatible_provider.hpp>

#include <kasli/model/model_prompt.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <utility>

#if KASLI_HAS_CURL
#include <curl/curl.h>
#endif

namespace kasli::model {
namespace {

#if KASLI_HAS_CURL
size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}
#endif

std::string trim_trailing_slashes(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

}  // namespace

std::string build_openai_chat_completions_payload_for_test(const ModelRequest& request,
                                                           const std::string& model) {
  const auto payload = nlohmann::json{
      {"model", model},
      {"messages",
       nlohmann::json::array({nlohmann::json{
           {"role", "user"},
           {"content", build_evidence_prompt(request)},
       }})},
      {"stream", false},
  };
  return payload.dump();
}

std::string parse_openai_chat_completions_response_for_test(const std::string& body) {
  const auto json = nlohmann::json::parse(body);
  return json.at("choices").at(0).at("message").at("content").get<std::string>();
}

std::string openai_chat_completions_endpoint_for_test(const std::string& endpoint) {
  const std::string suffix = "/chat/completions";
  auto trimmed = trim_trailing_slashes(endpoint);
  if (trimmed.ends_with(suffix)) {
    return trimmed;
  }
  return trimmed + suffix;
}

OpenAICompatibleProvider::OpenAICompatibleProvider(std::string endpoint, std::string model)
    : endpoint_(std::move(endpoint)), model_(std::move(model)) {}

std::string OpenAICompatibleProvider::complete(const ModelRequest& request) const {
#if KASLI_HAS_CURL
  std::string body;
  CURL* raw_curl = curl_easy_init();
  if (raw_curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }

  struct CurlHandle {
    CURL* handle;
    ~CurlHandle() { curl_easy_cleanup(handle); }
  } curl{raw_curl};

  curl_slist* raw_headers = nullptr;
  raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
  if (raw_headers == nullptr) {
    throw std::runtime_error("curl_slist_append failed");
  }

  struct HeaderList {
    curl_slist* headers;
    ~HeaderList() { curl_slist_free_all(headers); }
  } headers{raw_headers};

  const std::string url = openai_chat_completions_endpoint_for_test(endpoint_);
  const std::string payload_text = build_openai_chat_completions_payload_for_test(request, model_);

  curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_POST, 1L);
  curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, headers.headers);
  curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, payload_text.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload_text.size()));
  curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT, 2L);
  curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 4L);

  const CURLcode result = curl_easy_perform(curl.handle);
  if (result != CURLE_OK) {
    throw std::runtime_error(std::string("openai-compatible request failed: ") +
                             curl_easy_strerror(result));
  }

  long status_code = 0;
  curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status_code);
  if (status_code < 200 || status_code >= 300) {
    throw std::runtime_error("openai-compatible request failed with HTTP " +
                             std::to_string(status_code));
  }

  return parse_openai_chat_completions_response_for_test(body);
#else
  (void)request;
  throw std::runtime_error("openai-compatible provider requires libcurl");
#endif
}

}  // namespace kasli::model
