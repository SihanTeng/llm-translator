// Unit tests for DeepSeekClient::parseDelta (SSE payload parsing).

#include "../src/DeepSeekClient.h"

#include <QtTest>

class TestSseParsing : public QObject {
    Q_OBJECT

private slots:
    void deltas_data();
    void deltas();
};

void TestSseParsing::deltas_data() {
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("expected");

    QTest::newRow("content delta")
        << QByteArray(R"({"choices":[{"delta":{"content":"Hello"},"index":0}]})") << "Hello";
    QTest::newRow("unicode content")
        << QByteArray(R"({"choices":[{"delta":{"content":"你好"}}]})") << QString::fromUtf8("你好");
    QTest::newRow("empty content") << QByteArray(R"({"choices":[{"delta":{"content":""}}]})") << "";
    QTest::newRow("role-only first chunk")
        << QByteArray(R"({"choices":[{"delta":{"role":"assistant"}}]})") << "";
    QTest::newRow("finish chunk no delta content")
        << QByteArray(R"({"choices":[{"delta":{},"finish_reason":"stop"}])") << "";
    QTest::newRow("empty choices") << QByteArray(R"({"choices":[]})") << "";
    QTest::newRow("invalid json") << QByteArray("not json {") << "";
    QTest::newRow("empty payload") << QByteArray("") << "";
}

void TestSseParsing::deltas() {
    QFETCH(QByteArray, payload);
    QFETCH(QString, expected);
    QCOMPARE(DeepSeekClient::parseDelta(payload), expected);
}

QTEST_APPLESS_MAIN(TestSseParsing)
#include "tst_sseparsing.moc"
