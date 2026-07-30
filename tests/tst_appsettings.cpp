// Unit tests for AppSettings persistence (isolated via XDG_CONFIG_HOME set
// before main()) and the DEEPSEEK_API_KEY override.

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
    void envKeyOverrides();
    void cleanupTestCase();
};

void TestAppSettings::defaults() {
    const AppSettings s = AppSettings::load();
    QCOMPARE(s.baseUrl, QStringLiteral("https://api.deepseek.com"));
    QCOMPARE(s.model, QStringLiteral("deepseek-v4-flash"));
    QCOMPARE(s.targetLanguage, QStringLiteral("zh-CN"));
    QVERIFY(s.excludedApps.isEmpty());
    QVERIFY(s.autoUpdate);
    QVERIFY(s.apiKey.isEmpty());
}

void TestAppSettings::roundtrip() {
    AppSettings s;
    s.apiKey = QStringLiteral("sk-test-123");
    s.baseUrl = QStringLiteral("https://example.com");
    s.model = QStringLiteral("deepseek-v4-pro");
    s.targetLanguage = QStringLiteral("en");
    s.excludedApps = { QStringLiteral("keepassxc"), QStringLiteral("org.gnome.Terminal") };
    s.autoUpdate = false;
    s.save();

    const AppSettings loaded = AppSettings::load();
    QCOMPARE(loaded.apiKey, s.apiKey);
    QCOMPARE(loaded.baseUrl, s.baseUrl);
    QCOMPARE(loaded.model, s.model);
    QCOMPARE(loaded.targetLanguage, s.targetLanguage);
    QCOMPARE(loaded.excludedApps, s.excludedApps);
    QCOMPARE(loaded.autoUpdate, s.autoUpdate);
}

void TestAppSettings::legacyZhMigratesToZhCn() {
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

void TestAppSettings::envKeyOverrides() {
    AppSettings s;
    s.apiKey = QStringLiteral("sk-stored");
    if (qEnvironmentVariableIsSet("DEEPSEEK_API_KEY"))
        QCOMPARE(s.effectiveApiKey(), qgetenv("DEEPSEEK_API_KEY"));
    else
        QCOMPARE(s.effectiveApiKey(), QStringLiteral("sk-stored"));

    s.apiKey.clear();
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
