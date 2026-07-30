#include "LlmClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

using namespace Qt::StringLiterals;

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this)) { }

LlmClient *LlmClient::create(const ProviderInfo &provider, QObject *parent) {
    if (provider.style == ApiStyle::Anthropic)
        return new AnthropicClient(parent);
    return new OpenAiCompatClient(parent);
}

void LlmClient::configure(const ProviderInfo &provider, const QString &apiKey, const QString &model,
    const QString &baseUrlOverride) {
    m_provider = provider;
    m_apiKey = apiKey;
    m_model = model;
    m_baseUrl = baseUrlOverride.isEmpty() ? QString::fromUtf8(provider.baseUrl) : baseUrlOverride;
}

void LlmClient::translate(const QString &text, const QString &systemPrompt, bool jsonMode) {
    cancel();

    if (m_apiKey.isEmpty()) {
        const QString name = QString::fromUtf8(m_provider.name);
        const QString envVar = QString::fromUtf8(m_provider.envVar);
        emit errorOccurred(envVar.isEmpty()
                ? tr("No API key configured for %1. Open Settings to add one.").arg(name)
                : tr("No API key configured for %1. Open Settings to add one, or set the %2 "
                     "environment variable.")
                      .arg(name, envVar));
        return;
    }

    m_jsonMode = jsonMode;
    m_streamedRequest = shouldStream(jsonMode);
    m_streamed.clear();
    m_buffer.clear();

    QNetworkRequest request { QUrl(endpoint()) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);
    applyHeaders(request);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_reply = m_nam->post(request, buildBody(text, systemPrompt, jsonMode));
    connect(m_reply, &QNetworkReply::readyRead, this, &LlmClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &LlmClient::onFinished);
}

void LlmClient::cancel() {
    // Clear m_reply first: abort() can emit finished() synchronously, and
    // onFinished() would otherwise deleteLater() the same reply a second time.
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->abort();
    reply->deleteLater();
}

void LlmClient::onReadyRead() {
    if (!m_reply)
        return;

    m_buffer += m_reply->readAll();
    if (m_jsonMode && !m_streamedRequest)
        return; // the whole body is parsed in onFinished()

    // SSE: consume complete "data: <json>" lines as they arrive. Anthropic
    // interleaves "event:" lines, which this skips like any non-data line.
    while (true) {
        const int nl = m_buffer.indexOf('\n');
        if (nl < 0)
            break;
        const QByteArray line = m_buffer.left(nl).trimmed();
        m_buffer.remove(0, nl + 1);

        if (!line.startsWith("data:"))
            continue;
        const QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]")
            continue;

        const QString delta = parseStreamPayload(payload);
        if (delta.isEmpty())
            continue;
        if (m_jsonMode)
            m_streamed += delta;
        else
            emit tokenReceived(delta);
    }
}

QString LlmClient::parseFullBody(const QByteArray &body) const {
    return QString::fromUtf8(body);
}

void LlmClient::onFinished() {
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;

    // Drain any final chunk before evaluating the result. Skipped for
    // non-streamed JSON mode: the whole body is needed intact below.
    if (m_streamedRequest)
        onReadyRead();
    m_reply = nullptr;

    const QNetworkReply::NetworkError error = reply->error();
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString detail = QString::fromUtf8(reply->readAll()).trimmed();
        if (detail.size() > 300)
            detail = detail.left(300) + u"…"_s;
        emit errorOccurred(status > 0 ? tr("%1 request failed (HTTP %2): %3")
                                            .arg(QString::fromUtf8(m_provider.name))
                                            .arg(status)
                                            .arg(detail)
                                      : tr("Network error: %1").arg(reply->errorString()));
    }

    // For non-streamed JSON mode, readyRead() may not have consumed the
    // final bytes yet — read them now.
    const QString jsonContent = error == QNetworkReply::NoError && m_jsonMode && !m_streamedRequest
        ? parseFullBody(m_buffer + reply->readAll())
        : QString();

    reply->deleteLater();
    if (error == QNetworkReply::NoError) {
        if (m_jsonMode) {
            // Deliver the whole structured response as a single token.
            const QString content = m_streamedRequest ? m_streamed : jsonContent;
            if (!content.isEmpty())
                emit tokenReceived(content);
        }
        emit requestFinished();
    }
}

// ---- OpenAiCompatClient ---------------------------------------------------

QString OpenAiCompatClient::endpoint() const {
    return m_baseUrl + "/chat/completions"_L1;
}

void OpenAiCompatClient::applyHeaders(QNetworkRequest &request) const {
    // Bearer works for every documented OpenAI-compatible provider (MiMo
    // also offers an "api-key" header, but its docs' SDK path uses Bearer).
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
}

bool OpenAiCompatClient::shouldStream(bool jsonMode) const {
    // response_format JSON mode is requested non-streamed (single body);
    // PromptOnly providers stream and buffer instead.
    return !jsonMode || m_provider.jsonMode == JsonMode::PromptOnly;
}

QByteArray OpenAiCompatClient::buildBody(
    const QString &text, const QString &systemPrompt, bool jsonMode) const {
    QJsonObject body {
        { "model"_L1, m_model },
        { "stream"_L1, shouldStream(jsonMode) },
        { "messages"_L1,
            QJsonArray {
                QJsonObject { { "role"_L1, "system"_L1 }, { "content"_L1, systemPrompt } },
                QJsonObject { { "role"_L1, "user"_L1 }, { "content"_L1, text } },
            } },
    };
    // Low-latency popup: disable thinking mode where the provider has one
    // (DeepSeek v4 defaults it to enabled).
    if (m_provider.disableThinking)
        body["thinking"_L1] = QJsonObject { { "type"_L1, "disabled"_L1 } };
    if (jsonMode && m_provider.jsonMode == JsonMode::ResponseFormat)
        body["response_format"_L1] = QJsonObject { { "type"_L1, "json_object"_L1 } };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString OpenAiCompatClient::parseDelta(const QByteArray &payload) {
    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return { };

    const QJsonArray choices = doc["choices"_L1].toArray();
    if (choices.isEmpty())
        return { };
    return choices[0].toObject()["delta"_L1].toObject()["content"_L1].toString();
}

QString OpenAiCompatClient::parseStreamPayload(const QByteArray &payload) const {
    return parseDelta(payload);
}

QString OpenAiCompatClient::parseFullBody(const QByteArray &body) const {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray choices = doc["choices"_L1].toArray();
    if (choices.isEmpty())
        return { };
    return choices[0].toObject()["message"_L1].toObject()["content"_L1].toString();
}

// ---- AnthropicClient ------------------------------------------------------

QString AnthropicClient::endpoint() const {
    return m_baseUrl + "/v1/messages"_L1;
}

void AnthropicClient::applyHeaders(QNetworkRequest &request) const {
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("Accept", "text/event-stream");
}

bool AnthropicClient::shouldStream(bool jsonMode) const {
    Q_UNUSED(jsonMode);
    return true; // recommended by the docs; JSON mode buffers the deltas
}

QByteArray AnthropicClient::buildBody(
    const QString &text, const QString &systemPrompt, bool jsonMode) const {
    Q_UNUSED(jsonMode);
    QJsonObject body {
        { "model"_L1, m_model },
        // Required by the API; an upper bound only — translations stop far
        // earlier with stop_reason end_turn.
        { "max_tokens"_L1, 4096 },
        { "system"_L1, systemPrompt },
        { "messages"_L1,
            QJsonArray {
                QJsonObject { { "role"_L1, "user"_L1 }, { "content"_L1, text } },
            } },
        { "stream"_L1, true },
    };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString AnthropicClient::parseDelta(const QByteArray &payload) {
    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return { };
    if (doc["type"_L1].toString() != "content_block_delta"_L1)
        return { };
    const QJsonObject delta = doc["delta"_L1].toObject();
    if (delta["type"_L1].toString() != "text_delta"_L1)
        return { };
    return delta["text"_L1].toString();
}

QString AnthropicClient::parseStreamPayload(const QByteArray &payload) const {
    return parseDelta(payload);
}
