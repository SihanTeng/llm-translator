// Unit tests for the target-language list: unique codes, name resolution,
// and every flag icon present in the embedded resources (guards against
// typos in the flag codes). The .qrc is compiled into this test binary.

#include "../core/Languages.h"

#include <QtTest>

class TestLanguages : public QObject {
    Q_OBJECT

private slots:
    void codesAreUnique();
    void namesResolve();
    void flagsExist();
};

void TestLanguages::codesAreUnique() {
    QSet<QString> codes;
    for (const TargetLanguage &lang : targetLanguages()) {
        const QString code = QString::fromUtf8(lang.code);
        QVERIFY2(!codes.contains(code), qPrintable(code));
        codes.insert(code);
    }
    QVERIFY(targetLanguages().size() >= 20);
}

void TestLanguages::namesResolve() {
    QCOMPARE(languageEnglishName(QStringLiteral("zh-CN")), QStringLiteral("Simplified Chinese"));
    QCOMPARE(languageEnglishName(QStringLiteral("en")), QStringLiteral("English"));
    QCOMPARE(languageEnglishName(QStringLiteral("ja")), QStringLiteral("Japanese"));
    QVERIFY(languageEnglishName(QStringLiteral("bogus")).isEmpty());
    QVERIFY(languageEnglishName(QString()).isEmpty());
}

void TestLanguages::flagsExist() {
    for (const TargetLanguage &lang : targetLanguages()) {
        const QString path = QStringLiteral(":/flags/%1.png").arg(QString::fromUtf8(lang.flag));
        QVERIFY2(QFile::exists(path), qPrintable(path));
    }
}

QTEST_MAIN(TestLanguages)
#include "tst_languages.moc"
