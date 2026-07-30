// Validates the embedded spec/ JSON files through the core API: every
// future client implements against these files, so shape mistakes here
// break more than this app.

#include "../core/Prompts.h"
#include "../core/Provider.h"

#include <QtTest>

class TestProviderSpec : public QObject {
    Q_OBJECT

private slots:
    void loadsAllProviders();
    void fieldsAreSane();
    void idsAreUnique();
    void promptsFillTarget();
};

void TestProviderSpec::loadsAllProviders() {
    QVERIFY(providers().size() >= 8);
    QVERIFY(providerById(QStringLiteral("deepseek")) != nullptr);
    QVERIFY(providerById(QStringLiteral("anthropic")) != nullptr);
    QVERIFY(providerById(QStringLiteral("custom")) != nullptr);
    QVERIFY(providerById(QStringLiteral("bogus")) == nullptr);
}

void TestProviderSpec::fieldsAreSane() {
    for (const ProviderInfo &info : providers()) {
        QVERIFY2(!info.id.isEmpty() && !info.name.isEmpty(), qPrintable(info.id));
        QVERIFY2(!info.defaultModel.isEmpty(), qPrintable(info.id));
        if (info.id == QStringLiteral("custom")) {
            QVERIFY2(info.baseUrl.isEmpty(), "custom must not hardcode a base URL");
        } else {
            QVERIFY2(info.baseUrl.startsWith(QStringLiteral("https://")), qPrintable(info.id));
        }
        // The Anthropic API has no response_format parameter.
        if (info.style == ApiStyle::Anthropic)
            QVERIFY2(info.jsonMode == JsonMode::PromptOnly, qPrintable(info.id));
    }
    QCOMPARE(providerById(QStringLiteral("deepseek"))->style, ApiStyle::OpenAiCompatible);
    QVERIFY(providerById(QStringLiteral("deepseek"))->disableThinking);
    QCOMPARE(providerById(QStringLiteral("anthropic"))->style, ApiStyle::Anthropic);
}

void TestProviderSpec::idsAreUnique() {
    QSet<QString> ids;
    for (const ProviderInfo &info : providers()) {
        QVERIFY2(!ids.contains(info.id), qPrintable(info.id));
        ids.insert(info.id);
    }
}

void TestProviderSpec::promptsFillTarget() {
    const QString phrase = Prompts::phrase(QStringLiteral("German"));
    QVERIFY(phrase.contains(QStringLiteral("German")));
    QVERIFY(!phrase.contains(QStringLiteral("{target}")));
    const QString word = Prompts::word(QStringLiteral("German"));
    QVERIFY(word.contains(QStringLiteral("German")));
    QVERIFY(!word.contains(QStringLiteral("{target}")));
    // The dictionary-card contract must survive in the word prompt.
    QVERIFY(word.contains(QStringLiteral("\"type\": \"word\"")));
    QVERIFY(word.contains(QStringLiteral("\"type\": \"phrase\"")));
}

QTEST_MAIN(TestProviderSpec)
#include "tst_providerspec.moc"
