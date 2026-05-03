# Seamless Install And Local Model Provider Design

## Goal

After installing the RPM, a new Fedora user should be able to run `kasli`
commands without manually starting the daemon or passing socket paths. AI
answering should work with either Ollama or LM Studio-style OpenAI-compatible
servers after the user chooses and configures a local model.

## Scope

In scope:

- make installed CLI commands auto-start `kaslid.service` when the default
  socket is missing
- make the default socket path work even when `XDG_RUNTIME_DIR` is absent by
  falling back to `/run/user/$UID/kaslid.sock` on Linux
- add runtime model configuration for Ollama and OpenAI-compatible providers
- add an OpenAI-compatible chat-completions provider for LM Studio
- update README/PROJECT/PROGRESS with full install, daemon, Ollama, LM Studio,
  Gemma, Kimi, smoke-test, and troubleshooting instructions

Out of scope:

- bundling Ollama, LM Studio, or model weights inside the Kasli RPM
- downloading models automatically
- starting LM Studio or Ollama automatically
- supporting remote/cloud model providers by default
- enabling `kaslid.service` for future logins without the user's explicit
  command

## Installed CLI Startup Behavior

The CLI keeps the current explicit `--socket` behavior. When the user does not
pass `--socket`, the CLI uses the default socket path and treats itself as the
installed user entry point.

Default socket resolution:

1. `$XDG_RUNTIME_DIR/kaslid.sock`
2. `/run/user/$UID/kaslid.sock` on Linux when that directory exists
3. `kaslid.sock` for development and non-Linux fallback

If the first connection to the default socket fails because the socket is
missing, the CLI runs:

```sh
systemctl --user start kaslid
```

It then waits briefly for the socket to appear and retries the request. If the
service cannot be started or the socket never appears, the CLI returns a clear
error that tells the user to run `systemctl --user status kaslid`.

The CLI must not auto-start the service when the user passed `--socket`; an
explicit socket means the caller is managing the daemon path.

## Model Provider Configuration

The daemon reads model provider settings from environment variables at startup:

```text
KASLI_MODEL_PROVIDER=ollama
KASLI_MODEL_ENDPOINT=http://127.0.0.1:11434
KASLI_MODEL_NAME=gemma4
```

Supported providers:

- `ollama`: uses Ollama's native non-streaming `POST /api/generate`
- `openai-compatible`: uses non-streaming `POST /v1/chat/completions`

Defaults:

- provider: `ollama`
- Ollama endpoint: `http://127.0.0.1:11434`
- Ollama model: `gemma4`
- OpenAI-compatible endpoint: `http://127.0.0.1:1234/v1`
- OpenAI-compatible model: `local-model`

LM Studio users should set `KASLI_MODEL_PROVIDER=openai-compatible` and
`KASLI_MODEL_NAME` to the exact model identifier shown by:

```sh
curl http://127.0.0.1:1234/v1/models
```

The OpenAI-compatible provider sends the same constrained Kasli prompt and
curated evidence that Ollama receives. The model still never calls tools
directly.

## Systemd User Configuration

The RPM continues to install only the user unit. It does not auto-enable
autostart. Users configure model environment variables with a user drop-in:

```sh
systemctl --user edit kaslid
```

Example:

```ini
[Service]
Environment=KASLI_MODEL_PROVIDER=openai-compatible
Environment=KASLI_MODEL_ENDPOINT=http://127.0.0.1:1234/v1
Environment=KASLI_MODEL_NAME=local-model
```

For LM Studio, replace `local-model` with the exact model id returned by the
local `/v1/models` endpoint.

After editing:

```sh
systemctl --user daemon-reload
systemctl --user restart kaslid
```

## Documentation Requirements

The README must include:

- install the RPM
- run `kasli --tools-list` directly after install
- explain that the CLI starts the user daemon on demand
- start/stop/status commands for users who want manual control
- enable/disable commands for users who want login autostart
- Ollama setup with `ollama pull gemma4`, plus notes for Gemma 3 and Kimi
  variants when available in the user's model library
- LM Studio setup using the local server at `http://127.0.0.1:1234/v1`
- model configuration through `systemctl --user edit kaslid`
- smoke tests for `--ask`
- troubleshooting for missing socket, inactive service, model server down, and
  model name not found

## Testing

Automated tests should cover:

- default socket fallback to `/run/user/$UID/kaslid.sock`
- CLI daemon auto-start decision only for default sockets
- OpenAI-compatible response parsing
- OpenAI-compatible request payload shape
- model configuration defaulting and environment override behavior

Manual verification should cover:

- installed `kasli --tools-list` starts `kaslid.service` from inactive state
- installed `kasli --call-tool system.info` succeeds without `--socket`
- Ollama-backed `kasli --ask ...` works with an installed local model
- LM Studio-backed `kasli --ask ...` works with a loaded model

## References

- Ollama API base URL and `POST /api/generate`:
  https://docs.ollama.com/api
- Ollama non-streaming generation:
  https://docs.ollama.com/api/streaming
- Ollama Gemma 4 library entry:
  https://ollama.com/library/gemma4
- Ollama Kimi model entries:
  https://ollama.com/library/kimi-k2
- LM Studio OpenAI-compatible endpoints:
  https://lmstudio.ai/docs/app/api/endpoints/openai/
- LM Studio model listing endpoint:
  https://lmstudio.ai/docs/developer/openai-compat/models
