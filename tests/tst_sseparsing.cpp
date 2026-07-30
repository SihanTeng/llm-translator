// Unit tests for the SSE payload parsers: OpenAiCompatClient::parseDelta
// (OpenAI-style data chunks) and AnthropicClient::parseDelta (Messages API
// event stream).

#include "../src/LlmClient.h"

#include <QtTest>

class TestSseParsing : public QObject {
    Q_OBJECT

private slots:
    void openAiDeltas_data();
    void openAiDeltas();
    void anthropicDeltas_data();
    void anthropicDeltas();
};

void TestSseParsing::openAiDeltas_data() {
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("expected");

    QTest::newRow("content delta")
        << QByteArray(R"({"choices":[{"delta":{"content":"Hello"},"index":0}]})") << "Hello";
    QTest::newRow("unicode content")
        << QByteArray(R"({"choices":[{"delta":{"content":"你好"}}]})") << QString::fromUtf8("你好");
    QTest::newRow("empty content") << QByteArray(R"({"choices":[{"delta":{"content":""}}])") << "";
    QTest::newRow("role-only first chunk")
        << QByteArray(R"({"choices":[{"delta":{"role":"assistant"}}]})") << "";
    QTest::newRow("finish chunk no delta content")
        << QByteArray(R"({"choices":[{"delta":{},"finish_reason":"stop"}])") << "";
    QTest::newRow("empty choices") << QByteArray(R"({"choices":[]})") << "";
    QTest::newRow("invalid json") << QByteArray("not json {") << "";
    QTest::newRow("empty payload") << QByteArray("") << "";
}

void TestSseParsing::openAiDeltas() {
    QFETCH(QByteArray, payload);
    QFETCH(QString, expected);
    QCOMPARE(OpenAiCompatClient::parseDelta(payload), expected);
}

void TestSseParsing::anthropicDeltas_data() {
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("expected");

    QTest::newRow("text delta") << QByteArray(
        R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})")
                                << "Hello";
    QTest::newRow("unicode text delta") << QByteArray(
        R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"你好"}})")
                                        << QString::fromUtf8("你好");
    QTest::newRow("message_start")
        << QByteArray(R"({"type":"message_start","message":{"id":"msg_1","content":[]}})") << "";
    QTest::newRow("content_block_start") << QByteArray(
        R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})")
                                         << "";
    QTest::newRow("message_delta stop")
        << QByteArray(R"({"type":"message_delta","delta":{"stop_reason":"end_turn"}})") << "";
    QTest::newRow("message_stop") << QByteArray(R"({"type":"message_stop"})") << "";
    QTest::newRow("ping") << QByteArray(R"({"type":"ping"})") << "";
    QTest::newRow("thinking delta ignored") << QByteArray(
        R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"hmm"}})")
                                            << "";
    QTest::newRow("invalid json") << QByteArray("not json {") << "";
    QTest::newRow("empty payload") << QByteArray("") << "";
}

void TestSseParsing::anthropicDeltas() {
    QFETCH(QByteArray, payload);
    QFETCH(QString, expected);
    QCOMPARE(AnthropicClient::parseDelta(payload), expected);
}

QTEST_APPLESS_MAIN(TestSseParsing)
#include "tst_sseparsing.moc"
