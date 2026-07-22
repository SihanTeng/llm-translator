// Smoke test: streams from a (mock) OpenAI-compatible SSE endpoint using
// DeepSeekClient and prints every token received. Exit code 0 means the
// stream parsed cleanly from first token to [DONE].
//
// Usage: sse_client_test <baseUrl> <text>

#include "../src/DeepSeekClient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        qWarning("usage: %s <baseUrl> <text>", argv[0]);
        return 2;
    }

    DeepSeekClient client;
    client.setApiKey(QStringLiteral("test-key"));
    client.setBaseUrl(QString::fromLocal8Bit(argv[1]));

    QString accumulated;
    QObject::connect(&client, &DeepSeekClient::tokenReceived,
                     &app, [&accumulated](const QString &delta) {
        accumulated += delta;
        QTextStream(stdout) << "token: " << delta << '\n';
    });
    QObject::connect(&client, &DeepSeekClient::requestFinished, &app, [&] {
        QTextStream(stdout) << "DONE, full text: " << accumulated << '\n';
        QCoreApplication::exit(accumulated.isEmpty() ? 1 : 0);
    });
    QObject::connect(&client, &DeepSeekClient::errorOccurred, &app, [](const QString &message) {
        QTextStream(stderr) << "ERROR: " << message << '\n';
        QCoreApplication::exit(1);
    });

    client.translate(QString::fromLocal8Bit(argv[2]),
                     QStringLiteral("You are a translation engine."));

    QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(3); });
    return QCoreApplication::exec();
}
