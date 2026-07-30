#pragma once

#include <QList>
#include <QString>

// LLM provider registry (data-driven, like Languages.h). Adding a provider
// with an OpenAI-compatible API = one entry here; a genuinely different API
// shape additionally needs an LlmClient subclass selected via `style`.
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
    const char *id; // stable settings key ("deepseek")
    const char *name; // settings combo label ("DeepSeek")
    ApiStyle style;
    JsonMode jsonMode;
    const char *baseUrl; // default; only "custom" exposes it in Settings
    const char *defaultModel; // cheap/fast suggestion, first combo entry
    const char *altModel; // second combo suggestion ("" for none)
    const char *envVar; // API-key env override ("" for none)
    const char *keyPage; // where users create a key ("" for none)
    bool disableThinking; // send DeepSeek-style "thinking": {"type": "disabled"}
};

inline const QList<ProviderInfo> &providers() {
    static const QList<ProviderInfo> list {
        { "deepseek", "DeepSeek", ApiStyle::OpenAiCompatible, JsonMode::ResponseFormat,
            "https://api.deepseek.com", "deepseek-v4-flash", "deepseek-v4-pro", "DEEPSEEK_API_KEY",
            "https://platform.deepseek.com/api_keys", true },
        { "openai", "OpenAI", ApiStyle::OpenAiCompatible, JsonMode::ResponseFormat,
            "https://api.openai.com/v1", "gpt-5.6-luna", "gpt-5.6-terra", "OPENAI_API_KEY",
            "https://platform.openai.com/api-keys", false },
        { "gemini", "Google Gemini", ApiStyle::OpenAiCompatible, JsonMode::PromptOnly,
            "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-3.5-flash-lite",
            "gemini-3.6-flash", "GEMINI_API_KEY", "https://aistudio.google.com/apikey", false },
        { "anthropic", "Anthropic", ApiStyle::Anthropic, JsonMode::PromptOnly,
            "https://api.anthropic.com", "claude-haiku-4-5", "claude-sonnet-4-6",
            "ANTHROPIC_API_KEY", "https://platform.claude.com/settings/keys", false },
        { "grok", "Grok (xAI)", ApiStyle::OpenAiCompatible, JsonMode::ResponseFormat,
            "https://api.x.ai/v1", "grok-4-1-fast-non-reasoning", "grok-4.5", "XAI_API_KEY",
            "https://console.x.ai", false },
        { "openrouter", "OpenRouter", ApiStyle::OpenAiCompatible, JsonMode::ResponseFormat,
            "https://openrouter.ai/api/v1", "google/gemini-2.5-flash-lite", "openai/gpt-4o-mini",
            "OPENROUTER_API_KEY", "https://openrouter.ai/keys", false },
        { "mimo", "Xiaomi MiMo", ApiStyle::OpenAiCompatible, JsonMode::ResponseFormat,
            "https://api.xiaomimimo.com/v1", "mimo-v2.5", "mimo-v2.5-pro", "MIMO_API_KEY",
            "https://platform.xiaomimimo.com", false },
        { "custom", "Custom (OpenAI-compatible)", ApiStyle::OpenAiCompatible,
            JsonMode::ResponseFormat, "", "deepseek-v4-flash", "", "", "", false },
    };
    return list;
}

inline const ProviderInfo *providerById(const QString &id) {
    for (const ProviderInfo &info : providers()) {
        if (id == QLatin1StringView(info.id))
            return &info;
    }
    return nullptr;
}
