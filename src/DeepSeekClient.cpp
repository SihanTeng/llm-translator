#include "DeepSeekClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

using namespace Qt::StringLiterals;

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void DeepSeekClient::translate(const QString &text, const QString &systemPrompt)
{
    cancel();

    if (m_apiKey.isEmpty()) {
        emit errorOccurred(tr("No API key configured. Open Settings to add your DeepSeek API key, "
                              "or set the DEEPSEEK_API_KEY environment variable."));
        return;
    }

    const QJsonObject body{
        {"model"_L1, m_model},
        {"stream"_L1, true},
        // Low-latency popup: disable thinking mode (it defaults to enabled on v4 models).
        {"thinking"_L1, QJsonObject{{"type"_L1, "disabled"_L1}}},
        {"messages"_L1, QJsonArray{
            QJsonObject{{"role"_L1, "system"_L1}, {"content"_L1, systemPrompt}},
            QJsonObject{{"role"_L1, "user"_L1}, {"content"_L1, text}},
        }},
    };

    QNetworkRequest request(QUrl(m_baseUrl + "/chat/completions"_L1));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);
    request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_buffer.clear();
    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &DeepSeekClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &DeepSeekClient::onFinished);
}

void DeepSeekClient::cancel()
{
    // Clear m_reply first: abort() can emit finished() synchronously, and
    // onFinished() would otherwise deleteLater() the same reply a second time.
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->abort();
    reply->deleteLater();
}

void DeepSeekClient::onReadyRead()
{
    if (!m_reply)
        return;

    m_buffer += m_reply->readAll();

    // SSE: consume complete "data: <json>" lines as they arrive.
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

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            continue;

        const QJsonArray choices = doc["choices"_L1].toArray();
        if (choices.isEmpty())
            continue;
        const QString delta = choices[0].toObject()["delta"_L1].toObject()["content"_L1].toString();
        if (!delta.isEmpty())
            emit tokenReceived(delta);
    }
}

void DeepSeekClient::onFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;

    // Drain any final chunk before evaluating the result.
    onReadyRead();
    m_reply = nullptr;

    const QNetworkReply::NetworkError error = reply->error();
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString detail = QString::fromUtf8(reply->readAll()).trimmed();
        if (detail.size() > 300)
            detail = detail.left(300) + "…"_L1;
        emit errorOccurred(status > 0
            ? tr("DeepSeek request failed (HTTP %1): %2").arg(status).arg(detail)
            : tr("Network error: %1").arg(reply->errorString()));
    }

    reply->deleteLater();
    if (error == QNetworkReply::NoError)
        emit requestFinished();
}
