// Unit tests for Updater::isNewer (pure semver-ish compare).

#include "../src/Updater.h"

#include <QtTest>

class TestIsNewer : public QObject {
    Q_OBJECT

private slots:
    void compare_data();
    void compare();
};

void TestIsNewer::compare_data() {
    QTest::addColumn<QString>("latest");
    QTest::addColumn<QString>("current");
    QTest::addColumn<bool>("expected");

    QTest::newRow("patch bump") << "v0.1.3" << "0.1.2" << true;
    QTest::newRow("same version") << "0.1.2" << "0.1.2" << false;
    QTest::newRow("older") << "0.1.2" << "0.1.3" << false;
    QTest::newRow("major bump") << "v1.0.0" << "0.9.9" << true;
    QTest::newRow("short latest padded") << "0.2" << "0.1.9" << true;
    QTest::newRow("short vs padded equal") << "0.2" << "0.2.0" << false;
    QTest::newRow("minor beats patch") << "0.2.0" << "0.1.9" << true;
    QTest::newRow("no v prefix either side") << "2.0.1" << "2.0.0" << true;
}

void TestIsNewer::compare() {
    QFETCH(QString, latest);
    QFETCH(QString, current);
    QFETCH(bool, expected);
    QCOMPARE(Updater::isNewer(latest, current), expected);
}

QTEST_APPLESS_MAIN(TestIsNewer)
#include "tst_isnewer.moc"
