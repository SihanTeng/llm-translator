#pragma once

#include <QList>
#include <QString>

// LLM provider registry, parsed at runtime from spec/providers.json
// (embedded as :/spec/providers.json). The JSON file is the single source
// of truth shared with future non-C++ clients (Chrome extension, mobile
// apps); see spec/integration.md. Adding an OpenAI-compatible provider =
// one JSON entry; a new API shape additionally needs an LlmClient subclass
// selected via `style`.
//
// Endpoint/auth facts are from each provider's official docs (July 2026):
// DeepSeek platform.deepseek.com, OpenAI developers.openai.com, Gemini
// ai.google.dev (OpenAI-compat endpoint), Anthropic platform.claude.com,
// xAI docs.x.ai, OpenRouter openrouter.ai/docs, MiMo mimo.mi.com/docs.
enum class ApiStyle {
    OpenAiCompatible, // POST {base}/chat/completions, Bearer auth, SSE deltas
    Anthropic, // POST {base}/v1/messages, x-api-key auth, SSE event stream
};

enum class JsonMode {
    ResponseFormat, // may send response_format: {"type": "json_object"}
    PromptOnly, // no response_format; rely on the prompt, parse leniently
};

struct ProviderInfo {
    QString id; // stable settings key ("deepseek")
    QString name; // settings combo label ("DeepSeek")
    ApiStyle style = ApiStyle::OpenAiCompatible;
    JsonMode jsonMode = JsonMode::ResponseFormat;
    QString baseUrl; // default; only "custom" exposes it in Settings
    QString defaultModel; // cheap/fast suggestion, first combo entry
    QString altModel; // second combo suggestion ("" for none)
    QString envVar; // API-key env override ("" for none)
    QString keyPage; // where users create a key ("" for none)
    bool disableThinking = false; // send DeepSeek-style "thinking": {"type": "disabled"}
};

// All providers from spec/providers.json, in file order. Empty only if the
// embedded spec is missing/corrupt (tst_providerspec guards that).
const QList<ProviderInfo> &providers();

const ProviderInfo *providerById(const QString &id);
