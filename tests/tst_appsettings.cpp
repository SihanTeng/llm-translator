// Unit tests for AppSettings persistence (isolated via XDG_CONFIG_HOME set
// before main()): per-provider groups, legacy migrations, env overrides.

#include "../src/SettingsDialog.h"

#include <QSettings>
#include <QtTest>

// Must run before main(): QSettings' UserScope follows XDG_CONFIG_HOME, and
// it is read lazily but cached — setting it here guarantees isolation from
// the developer's real ~/.config.
static const struct ConfigIsolation {
    ConfigIsolation() {
        const QByteArray dir = "/tmp/translator-test-config-"
            + QByteArray::number(QCoreApplication::applicationPid());
        qputenv("XDG_CONFIG_HOME", dir);
    }
} s_isolation;

class TestAppSettings : public QObject {
    Q_OBJECT

private slots:
    void defaults();
    void roundtrip();
    void legacyZhMigratesToZhCn();
    void preProviderConfigMigrates();
    void envKeyOverrides();
    void cleanupTestCase();

private:
    static void clearStore() {
        QSettings store(QStringLiteral("translator"), QStringLiteral("translator"));
        store.clear();
    }
};

void TestAppSettings::defaults() {
    const AppSettings s = AppSettings::load();
    QCOMPARE(s.provider, QStringLiteral("deepseek"));
    QCOMPARE(s.targetLanguage, QStringLiteral("zh-CN"));
    QVERIFY(s.excludedApps.isEmpty());
    QVERIFY(s.autoUpdate);
    const ProviderSettings entry = s.currentProviderSettings();
    QVERIFY(entry.apiKey.isEmpty());
    QCOMPARE(entry.model, QStringLiteral("deepseek-v4-flash"));
}

void TestAppSettings::roundtrip() {
    AppSettings s;
    s.provider = QStringLiteral("anthropic");
    s.perProvider.insert(QStringLiteral("anthropic"),
        { QStringLiteral("sk-ant"), QStringLiteral("claude-haiku-4-5"), QString() });
    s.perProvider.insert(QStringLiteral("openai"),
        { QStringLiteral("sk-oai"), QStringLiteral("gpt-5.6-luna"), QString() });
    s.perProvider.insert(QStringLiteral("custom"),
        { QStringLiteral("sk-local"), QStringLiteral("llama3"),
            QStringLiteral("http://host:1/v1") });
    s.targetLanguage = QStringLiteral("ja");
    s.excludedApps = { QStringLiteral("keepassxc"), QStringLiteral("org.gnome.Terminal") };
    s.autoUpdate = false;
    s.save();

    const AppSettings loaded = AppSettings::load();
    QCOMPARE(loaded.provider, s.provider);
    QCOMPARE(loaded.targetLanguage, s.targetLanguage);
    QCOMPARE(loaded.excludedApps, s.excludedApps);
    QCOMPARE(loaded.autoUpdate, s.autoUpdate);
    QCOMPARE(
        loaded.perProvider.value(QStringLiteral("anthropic")).apiKey, QStringLiteral("sk-ant"));
    QCOMPARE(
        loaded.perProvider.value(QStringLiteral("openai")).model, QStringLiteral("gpt-5.6-luna"));
    QCOMPARE(loaded.perProvider.value(QStringLiteral("custom")).baseUrl,
        QStringLiteral("http://host:1/v1"));
    QCOMPARE(loaded.currentProviderSettings().apiKey, QStringLiteral("sk-ant"));
}

void TestAppSettings::legacyZhMigratesToZhCn() {
    clearStore();
    // Configs written before the multi-language list store "zh"; load()
    // must normalize it to "zh-CN" so the combo and prompts keep working.
    {
        QSettings store(QStringLiteral("translator"), QStringLiteral("translator"));
        store.setValue(QStringLiteral("targetLanguage"), QStringLiteral("zh"));
    }
    QCOMPARE(AppSettings::load().targetLanguage, QStringLiteral("zh-CN"));
    // The removed "auto" option migrates the same way.
    {
        QSettings store(QStringLiteral("translator"), QStringLiteral("translator"));
        store.setValue(QStringLiteral("targetLanguage"), QStringLiteral("auto"));
    }
    QCOMPARE(AppSettings::load().targetLanguage, QStringLiteral("zh-CN"));
}

void TestAppSettings::preProviderConfigMigrates() {
    clearStore();
    // Top-level apiKey/model with the default DeepSeek base URL become the
    // deepseek provider entry.
    {
        QSettings store(QStringLiteral("translator"), QStringLiteral("translator"));
        store.setValue(QStringLiteral("apiKey"), QStringLiteral("sk-old"));
        store.setValue(QStringLiteral("baseUrl"), QStringLiteral("https://api.deepseek.com"));
        store.setValue(QStringLiteral("model"), QStringLiteral("deepseek-v4-pro"));
    }
    AppSettings loaded = AppSettings::load();
    QCOMPARE(loaded.provider, QStringLiteral("deepseek"));
    QCOMPARE(loaded.perProvider.value(QStringLiteral("deepseek")).apiKey, QStringLiteral("sk-old"));
    QCOMPARE(loaded.perProvider.value(QStringLiteral("deepseek")).model,
        QStringLiteral("deepseek-v4-pro"));

    clearStore();
    // A custom base URL migrates to the "custom" provider.
    {
        QSettings store(QStringLiteral("translator"), QStringLiteral("translator"));
        store.setValue(QStringLiteral("apiKey"), QStringLiteral("sk-old"));
        store.setValue(QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:8955"));
        store.setValue(QStringLiteral("model"), QStringLiteral("my-model"));
    }
    loaded = AppSettings::load();
    QCOMPARE(loaded.provider, QStringLiteral("custom"));
    QCOMPARE(loaded.perProvider.value(QStringLiteral("custom")).baseUrl,
        QStringLiteral("http://127.0.0.1:8955"));
    QCOMPARE(loaded.perProvider.value(QStringLiteral("custom")).apiKey, QStringLiteral("sk-old"));
}

void TestAppSettings::envKeyOverrides() {
    clearStore();
    AppSettings s; // provider defaults to deepseek -> DEEPSEEK_API_KEY applies
    s.perProvider.insert(
        QStringLiteral("deepseek"), { QStringLiteral("sk-stored"), QString(), QString() });
    if (qEnvironmentVariableIsSet("DEEPSEEK_API_KEY"))
        QCOMPARE(s.effectiveApiKey(), qgetenv("DEEPSEEK_API_KEY"));
    else
        QCOMPARE(s.effectiveApiKey(), QStringLiteral("sk-stored"));

    s.perProvider[QStringLiteral("deepseek")].apiKey.clear();
    if (qEnvironmentVariableIsSet("DEEPSEEK_API_KEY"))
        QCOMPARE(s.effectiveApiKey(), qgetenv("DEEPSEEK_API_KEY"));
    else
        QVERIFY(s.effectiveApiKey().isEmpty());
}

void TestAppSettings::cleanupTestCase() {
    QDir(qgetenv("XDG_CONFIG_HOME")).removeRecursively();
}

QTEST_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
