#pragma once

#include "Provider.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// Streaming LLM client. The base class owns the network lifecycle (POST,
// SSE line splitting, cancel, error reporting); subclasses provide the
// provider-specific endpoint, headers, request body, and stream parsing.
//
// Two flows:
//  - streamed (all phrase translations, and JSON mode for PromptOnly
//    providers): deltas are emitted as tokens as they arrive (JSON mode
//    buffers them and emits one token at the end).
//  - whole-body (JSON mode for ResponseFormat providers): the request is
//    not streamed; the body is parsed in one go on finish.
class LlmClient : public QObject {
    Q_OBJECT

public:
    explicit LlmClient(QObject *parent = nullptr);

    // Creates the right subclass for the provider's API style.
    static LlmClient *create(const ProviderInfo &provider, QObject *parent = nullptr);

    void configure(const ProviderInfo &provider, const QString &apiKey, const QString &model,
        const QString &baseUrlOverride = { });

    // Starts a request, cancelling any in-flight one. jsonMode asks for a
    // structured JSON response (dictionary card) instead of a plain stream.
    void translate(const QString &text, const QString &systemPrompt, bool jsonMode = false);
    void cancel();

signals:
    void tokenReceived(const QString &delta);
    void requestFinished();
    void errorOccurred(const QString &message);

protected:
    virtual QString endpoint() const = 0;
    virtual void applyHeaders(QNetworkRequest &request) const = 0;
    virtual QByteArray buildBody(
        const QString &text, const QString &systemPrompt, bool jsonMode) const = 0;
    // Whether this request goes out with "stream": true.
    virtual bool shouldStream(bool jsonMode) const = 0;
    // Parses one SSE "data:" payload and returns the delta content (empty
    // when the payload carries no content).
    virtual QString parseStreamPayload(const QByteArray &payload) const = 0;
    // Parses a non-streamed response body (OpenAI JSON mode).
    virtual QString parseFullBody(const QByteArray &body) const;

    ProviderInfo m_provider { };
    QString m_apiKey;
    QString m_model;
    QString m_baseUrl;
    bool m_jsonMode = false;
    bool m_streamedRequest = false;

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_streamed; // JSON-mode accumulation for streamed requests
};

// OpenAI-compatible providers: DeepSeek, OpenAI, Gemini (compat endpoint),
// Grok, OpenRouter, MiMo, and custom endpoints (incl. Ollama).
class OpenAiCompatClient : public LlmClient {
    Q_OBJECT

public:
    using LlmClient::LlmClient;

    // Exposed for tests: parses one SSE "data:" JSON payload.
    static QString parseDelta(const QByteArray &payload);

protected:
    QString endpoint() const override;
    void applyHeaders(QNetworkRequest &request) const override;
    QByteArray buildBody(
        const QString &text, const QString &systemPrompt, bool jsonMode) const override;
    bool shouldStream(bool jsonMode) const override;
    QString parseStreamPayload(const QByteArray &payload) const override;
    QString parseFullBody(const QByteArray &body) const override;
};

// Anthropic Messages API (docs.claude.com/en/api/messages-streaming):
// x-api-key auth, top-level "system", content_block_delta text events.
class AnthropicClient : public LlmClient {
    Q_OBJECT

public:
    using LlmClient::LlmClient;

    // Exposed for tests: parses one SSE "data:" JSON payload.
    static QString parseDelta(const QByteArray &payload);

protected:
    QString endpoint() const override;
    void applyHeaders(QNetworkRequest &request) const override;
    QByteArray buildBody(
        const QString &text, const QString &systemPrompt, bool jsonMode) const override;
    bool shouldStream(bool jsonMode) const override;
    QString parseStreamPayload(const QByteArray &payload) const override;
};
