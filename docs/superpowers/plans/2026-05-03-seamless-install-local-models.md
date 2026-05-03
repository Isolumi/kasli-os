# Seamless Install And Local Models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the installed `kasli` CLI work immediately after RPM install and support both Ollama and LM Studio/OpenAI-compatible local model servers.

**Architecture:** Keep `kasli` as the user-facing entry point: when the default socket is missing, it starts `kaslid.service` through `systemctl --user` and retries. Keep model execution behind the existing `ModelProvider` interface, add runtime environment configuration, and add an OpenAI-compatible chat-completions provider alongside the existing Ollama provider.

**Tech Stack:** C++23, CMake, Catch2, CLI11, libcurl, Unix domain sockets, systemd user services, CPack RPM.

---

## Task 1: Default Socket Fallback And CLI Auto-Start

**Files:**
- Modify: `include/kasli/app/default_paths.hpp`
- Modify: `src/app/default_paths.cpp`
- Modify: `tests/unit/default_paths_test.cpp`
- Create: `include/kasli/app/daemon_autostart.hpp`
- Create: `src/app/daemon_autostart.cpp`
- Create: `tests/unit/daemon_autostart_test.cpp`
- Modify: `src/kasli-cli/main.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Add failing default-path and auto-start tests**

Add tests showing:

```cpp
TEST_CASE("default socket path falls back to Linux user runtime directory") {
  const auto path = kasli::app::detail::default_socket_path_from_env(
      env_lookup({}),
      [] { return std::optional<std::filesystem::path>("/run/user/1000"); });

  REQUIRE(path == std::filesystem::path("/run/user/1000/kaslid.sock"));
}
```

Create `tests/unit/daemon_autostart_test.cpp` with tests for:

- missing default socket starts the service and retries
- explicit socket never starts the service
- start failure reports `systemctl --user status kaslid`
- non-missing connection errors are returned without starting the service

- [x] **Step 2: Run tests to verify red**

Run:

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora --target kasli_default_paths_test kasli_daemon_autostart_test
```

Expected: build fails because the injected runtime-dir overload and daemon
auto-start helper do not exist yet.

- [x] **Step 3: Implement runtime-dir fallback**

Extend `default_socket_path_from_env` so it checks:

1. non-empty `XDG_RUNTIME_DIR`
2. an injected Linux runtime-dir lookup
3. `kaslid.sock`

The production lookup should return `/run/user/<geteuid()>` on Linux only when
that directory exists.

- [x] **Step 4: Implement daemon auto-start helper**

Add a helper with injectable dependencies:

```cpp
struct DaemonAutostartDeps {
  std::function<std::string(const std::filesystem::path&, const std::string&)> request;
  std::function<int()> start_user_service;
  std::function<bool(const std::filesystem::path&)> socket_exists;
  std::function<void(std::chrono::milliseconds)> sleep_for;
};

std::string request_with_optional_user_service_start(
    const std::filesystem::path& socket_path,
    const std::string& request,
    bool allow_autostart,
    const DaemonAutostartDeps& deps);
```

The default implementation should run `systemctl --user start kaslid`, wait up
to roughly two seconds for the socket, and retry once.

- [x] **Step 5: Wire CLI explicit/default socket behavior**

Capture the `--socket` option pointer before `CLI11_PARSE`. After parsing,
allow auto-start only when `socket_option->count() == 0`.

- [x] **Step 6: Verify green and commit**

Run:

```sh
cmake --build build-fedora --target kasli_default_paths_test kasli_daemon_autostart_test
./build-fedora/kasli_default_paths_test
./build-fedora/kasli_daemon_autostart_test
```

Commit:

```sh
git add CMakeLists.txt include/kasli/app src/app tests/unit/default_paths_test.cpp tests/unit/daemon_autostart_test.cpp src/kasli-cli/main.cpp
git commit -m "feat: auto-start user daemon from CLI"
```

## Task 2: OpenAI-Compatible Provider And Model Config

**Files:**
- Create: `include/kasli/model/model_prompt.hpp`
- Create: `src/model/model_prompt.cpp`
- Modify: `include/kasli/model/ollama_provider.hpp`
- Modify: `src/model/ollama_provider.cpp`
- Create: `include/kasli/model/openai_compatible_provider.hpp`
- Create: `src/model/openai_compatible_provider.cpp`
- Create: `include/kasli/model/model_config.hpp`
- Create: `src/model/model_config.cpp`
- Create: `tests/unit/openai_compatible_provider_test.cpp`
- Create: `tests/unit/model_config_test.cpp`
- Modify: `tests/unit/ollama_provider_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Add failing provider/config tests**

Add tests showing:

- OpenAI-compatible payload uses `model`, `stream=false`, and
  `messages[0].content` containing Kasli's constrained evidence prompt.
- OpenAI-compatible parser returns `choices[0].message.content`.
- OpenAI-compatible parser rejects a missing content field.
- default model config is provider `ollama`, endpoint
  `http://127.0.0.1:11434`, model `gemma4`.
- `KASLI_MODEL_PROVIDER=openai-compatible` defaults endpoint to
  `http://127.0.0.1:1234/v1` and model to `local-model`.
- `KASLI_MODEL_ENDPOINT` and `KASLI_MODEL_NAME` override provider defaults.
- unknown provider names throw a clear error.

- [x] **Step 2: Run tests to verify red**

Run:

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fedora --target kasli_openai_compatible_provider_test kasli_model_config_test
```

Expected: build fails because the provider and config files do not exist yet.

- [x] **Step 3: Extract shared model prompt**

Move the current prompt construction from `src/model/ollama_provider.cpp` into
`src/model/model_prompt.cpp` as:

```cpp
std::string build_evidence_prompt(const ModelRequest& request);
```

Keep `build_ollama_prompt_for_test` as a wrapper so existing tests keep their
public API.

- [x] **Step 4: Implement OpenAI-compatible provider**

Implement non-streaming `POST /v1/chat/completions` with libcurl. The payload
should use:

```json
{
  "model": "<configured model>",
  "messages": [
    {"role": "user", "content": "<Kasli evidence prompt>"}
  ],
  "stream": false
}
```

The response parser should return `choices[0].message.content`.

- [x] **Step 5: Implement model config**

Implement environment lookup helpers and:

```cpp
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
```

- [x] **Step 6: Verify green and commit**

Run:

```sh
cmake --build build-fedora --target kasli_ollama_provider_test kasli_openai_compatible_provider_test kasli_model_config_test
./build-fedora/kasli_ollama_provider_test
./build-fedora/kasli_openai_compatible_provider_test
./build-fedora/kasli_model_config_test
```

Commit:

```sh
git add CMakeLists.txt include/kasli/model src/model tests/unit/ollama_provider_test.cpp tests/unit/openai_compatible_provider_test.cpp tests/unit/model_config_test.cpp
git commit -m "feat: add configurable local model providers"
```

## Task 3: Daemon Model Provider Wiring

**Files:**
- Modify: `src/kaslid/main.cpp`
- Modify: `tests/unit/model_config_test.cpp`

- [x] **Step 1: Add failing integration-facing config test**

Add a test that `model_config_from_env` accepts:

```text
KASLI_MODEL_PROVIDER=ollama
KASLI_MODEL_ENDPOINT=http://127.0.0.1:11434
KASLI_MODEL_NAME=kimi-k2
```

and returns provider `ollama`, endpoint `http://127.0.0.1:11434`, model
`kimi-k2`.

- [x] **Step 2: Run test to verify red or confirm existing coverage**

Run:

```sh
cmake --build build-fedora --target kasli_model_config_test
./build-fedora/kasli_model_config_test
```

Expected before implementation: if Task 2 did not cover this exact case, the
new test fails.

- [x] **Step 3: Wire daemon to runtime model provider**

In `src/kaslid/main.cpp`, create the model provider once in `main`:

```cpp
const auto model_config = kasli::model::default_model_config();
auto model = kasli::model::make_model_provider(model_config);
```

Pass `*model` into `handle_request` and remove the hardcoded
`OllamaProvider("http://127.0.0.1:11434", "llama3.2")`.

- [x] **Step 4: Verify green and commit**

Run:

```sh
cmake --build build-fedora --target kaslid kasli_model_config_test
./build-fedora/kasli_model_config_test
```

Commit:

```sh
git add src/kaslid/main.cpp tests/unit/model_config_test.cpp
git commit -m "feat: configure daemon model provider"
```

## Task 4: README, Project Docs, RPM Version, And Verification

**Files:**
- Modify: `README.md`
- Modify: `PROJECT.md`
- Modify: `PROGRESS.md`
- Modify: `docs/research/fedora-server-test.md`
- Modify: `CMakeLists.txt`
- Modify: `docs/superpowers/plans/2026-05-03-seamless-install-local-models.md`

- [x] **Step 1: Update docs**

Document:

- `sudo dnf install ./build-fedora/kasli-os-0.1.1-1.*.rpm`
- `kasli --tools-list` directly after install
- CLI on-demand daemon startup
- manual `systemctl --user start|stop|status kaslid`
- optional `systemctl --user enable --now kaslid`
- Ollama setup with `ollama pull gemma4`
- optional Gemma 3 and Kimi model names such as `gemma3`, `kimi-k2`,
  `kimi-k2-thinking`, and cloud-only Kimi variants when applicable
- LM Studio server setup at `http://127.0.0.1:1234/v1`
- `curl http://127.0.0.1:1234/v1/models`
- `systemctl --user edit kaslid` examples for both providers
- `kasli --ask "What OS is this?" --ask-tool system.info`
- troubleshooting for missing socket, service failure, missing model server,
  and bad model name

- [x] **Step 2: Bump RPM version**

Change `project(kasli_os VERSION 0.1.0 LANGUAGES CXX)` to:

```cmake
project(kasli_os VERSION 0.1.1 LANGUAGES CXX)
```

- [x] **Step 3: Run full verification**

Run:

```sh
cmake -S . -B build-fedora -DCMAKE_BUILD_TYPE=Release
cmake --build build-fedora
ctest --test-dir build-fedora --output-on-failure
find build-fedora -maxdepth 1 -name '*.rpm' -delete
cpack -G RPM --config build-fedora/CPackConfig.cmake
rpm -qpl build-fedora/kasli-os-0.1.1-1.x86_64.rpm
rpm -U --test build-fedora/kasli-os-0.1.1-1.x86_64.rpm
git diff --check
```

- [x] **Step 4: Request final review and commit**

Request a code review over all changes. Address critical or important findings.
Then commit:

```sh
git add CMakeLists.txt README.md PROJECT.md PROGRESS.md docs/research/fedora-server-test.md docs/superpowers/plans/2026-05-03-seamless-install-local-models.md
git commit -m "docs: update seamless install and model setup"
```

## Self-Review

Spec coverage:

- Installed CLI auto-start is covered by Task 1.
- `/run/user/$UID` socket fallback is covered by Task 1.
- Ollama and OpenAI-compatible provider configuration is covered by Task 2.
- Daemon runtime model provider selection is covered by Task 3.
- README/PROJECT/PROGRESS/RPM verification is covered by Task 4.

The plan contains no blocked placeholders. Type names introduced in later tasks
are defined before use. Each implementation task has a focused verification
command and commit boundary.
