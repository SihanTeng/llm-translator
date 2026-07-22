#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Streaming client for DeepSeek's OpenAI-compatible chat completions API.
// Bring-your-own-key: the key is only ever sent to the configured base URL
// (https://api.deepseek.com by default).
class DeepSeekClient : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }

    // Starts a streaming request, cancelling any in-flight one.
    void translate(const QString &text, const QString &systemPrompt);
    void cancel();

signals:
    void tokenReceived(const QString &delta);
    void requestFinished();
    void errorOccurred(const QString &message);

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_apiKey;
    QString m_baseUrl = QStringLiteral("https://api.deepseek.com");
    QString m_model = QStringLiteral("deepseek-v4-flash");
    QByteArray m_buffer;
};
