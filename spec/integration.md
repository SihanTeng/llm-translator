# Building a translator client

The translation logic every platform shares lives in this directory as
**data, not code**. A new client (macOS, Windows, iOS, Android, Chrome
extension) implements against `providers.json` + `prompts.json` and the
contracts below; it does not need a server, and it never needs to proxy
anyone's API key.

## Repository layout

- `backend/` — the shared backend as a Rust crate: provider registry, LLM
  clients, prompts, word-card parsing, history store. Exposed to native
  clients via the C ABI in `backend/include/translator_backend.h`; tested
  with `cargo test`.
- `clients/` — one directory per platform client. `clients/linux-qt/` is
  the reference implementation (Qt Widgets shell + GNOME Shell bridge in
  `clients/linux-qt/extension/`). Replace the shell per platform; keep the
  backend.
- `spec/` — the cross-platform contract (this directory). Clients that do
  not link the Rust backend (e.g. a Chrome extension) implement against
  these files directly.

## Two integration models

1. **Full client** (Chrome extension, mobile, most apps): implement the
   provider API calls yourself per `providers.json` + `prompts.json`.
   Straightforward HTTPS+SSE; no dependency on this repo's runtime.
2. **Thin UI bridge + local backend** (how the GNOME extension works):
   a privileged shell layer captures selections and renders UI; the backend
   owns keys, settings, and provider traffic over a local IPC channel. Use
   this when the platform restricts background selection reading or window
   placement. Our reference IPC is the D-Bus interface `org.translator.App`
   (see `clients/linux-qt/src/AppController.h`): methods `TranslateSelection`,
   `TranslateSelectionWithContext`, `SetShellUiEnabled`, `ShowSettings`,
   `SpeakText`, `GetExcludedApps`; signals `TranslationToken`,
   `TranslationWordCard`, `TranslationFinished`, `TranslationError`,
   `ExcludedAppsChanged`. Map it to your platform's IPC (XPC, named pipe,
   native messaging).

## providers.json

Each entry:

| field | meaning |
|---|---|
| `id` | stable settings key, never rename |
| `name` | display name |
| `style` | `openai-compatible` or `anthropic` (request/response protocol) |
| `jsonMode` | `response_format` (send `response_format: {"type":"json_object"}`) or `prompt_only` (rely on the prompt; parse leniently) |
| `baseUrl` | default endpoint root; user-overridable only for `custom` |
| `defaultModel` / `altModel` | suggested cheap/fast and alternate model IDs |
| `envVar` | conventional API-key env var, overrides the stored key |
| `keyPage` | where users create a key |
| `disableThinking` | send `"thinking": {"type": "disabled"}` (DeepSeek) |

### Style: `openai-compatible`

- `POST {baseUrl}/chat/completions`, `Authorization: Bearer <key>`,
  body `{model, stream, messages: [{role:"system",...},{role:"user",...}]}`.
- Phrase translations stream (`stream: true`): SSE lines
  `data: {...}`, text at `choices[0].delta.content`, `data: [DONE]` ends.
- Word (dictionary) mode with `jsonMode: response_format`: non-streamed,
  `response_format: {"type":"json_object"}`, body at
  `choices[0].message.content`. With `prompt_only`: stream and buffer.

### Style: `anthropic`

- `POST {baseUrl}/v1/messages`, headers `x-api-key: <key>`,
  `anthropic-version: 2023-06-01`.
- Body `{model, max_tokens: 4096, system: <prompt>,
  messages: [{role:"user", content: <text>}], stream: true}`. `system` is
  top-level (no system role in `messages`); `max_tokens` is required.
- SSE `event:`/`data:` pairs; text at `content_block_delta` →
  `delta.text` where `delta.type == "text_delta"`. Ignore all other event
  types. Always stream; buffer the deltas in word mode.

## prompts.json

Two system prompts; replace `{target}` with the English target-language
name (e.g. `Simplified Chinese`):

- `phrase` — plain translation; user message is the raw selected text.
- `word` — used for short selections (≤ ~8 words). User message is
  `Text: <selection>` optionally followed by `\nSentence: <context>`.
  The model returns ONLY a JSON object (the word-card contract below).

## Word-card JSON contract

Word mode yields one JSON object:

```json
{"type": "word", "word": "...", "phonetic": "...", "pos": "...",
 "meaning": "...", "explanation": "...", "example": "..."}
```

or `{"type": "phrase", "translation": "..."}` when the model decides the
selection is a phrase. All fields are optional strings. Clients must
parse leniently (strip markdown fences/prose around the object — see
`backend/src/wordcard.rs`) and HTML-escape every field before rendering.

## Versioning

Both files carry a top-level `version` integer, bumped on incompatible
changes. Additive changes (new provider, new optional field) do not bump
it. The Rust backend tests validate the files' shape — run
`cargo test --manifest-path backend/Cargo.toml` after every spec edit.
