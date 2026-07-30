// Unit tests for AppPickerDialog::installedApps: .desktop parsing, hidden /
// NoDisplay filtering, WM_CLASS candidates, and first-dir-wins dedup.

#include "../src/AppPickerDialog.h"

#include <QtTest>

using namespace Qt::StringLiterals;

class TestAppPicker : public QObject {
    Q_OBJECT

private slots:
    void parsesDesktopFiles();
    void firstDirWins();

private:
    static void writeDesktop(const QString &dir, const QString &id, const QString &body) {
        QFile file(dir + u'/' + id + ".desktop"_L1);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(body.toUtf8());
    }
};

void TestAppPicker::parsesDesktopFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeDesktop(dir.path(), QStringLiteral("keepassxc"),
        QStringLiteral("[Desktop Entry]\nName=KeePassXC\nStartupWMClass=keepassxc\n"
                       "Icon=keepassxc\nExec=keepassxc %U\n"));
    writeDesktop(dir.path(), QStringLiteral("org.example.Zed"),
        QStringLiteral("[Desktop Entry]\nName=Zed Editor\nExec=zed\n"));
    writeDesktop(dir.path(), QStringLiteral("hidden-app"),
        QStringLiteral("[Desktop Entry]\nName=Hidden App\nHidden=true\n"));
    writeDesktop(dir.path(), QStringLiteral("nodisplay-app"),
        QStringLiteral("[Desktop Entry]\nName=NoShow App\nNoDisplay=true\n"));
    writeDesktop(
        dir.path(), QStringLiteral("noname-app"), QStringLiteral("[Desktop Entry]\nExec=noname\n"));

    const QList<InstalledApp> apps = AppPickerDialog::installedApps({ dir.path() });
    QCOMPARE(apps.size(), 2);
    // Sorted by name, case-insensitive.
    QCOMPARE(apps.at(0).name, QStringLiteral("KeePassXC"));
    QCOMPARE(apps.at(1).name, QStringLiteral("Zed Editor"));
    // StartupWMClass wins; the desktop id is the fallback candidate.
    QCOMPARE(apps.at(0).wmClasses, QStringList { QStringLiteral("keepassxc") });
    QCOMPARE(apps.at(1).wmClasses, QStringList { QStringLiteral("org.example.Zed") });
    QCOMPARE(apps.at(0).iconName, QStringLiteral("keepassxc"));
}

void TestAppPicker::firstDirWins() {
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid() && dirB.isValid());
    writeDesktop(
        dirA.path(), QStringLiteral("app"), QStringLiteral("[Desktop Entry]\nName=From A\n"));
    writeDesktop(
        dirB.path(), QStringLiteral("app"), QStringLiteral("[Desktop Entry]\nName=From B\n"));

    const QList<InstalledApp> apps = AppPickerDialog::installedApps({ dirA.path(), dirB.path() });
    QCOMPARE(apps.size(), 1);
    QCOMPARE(apps.constFirst().name, QStringLiteral("From A"));
}

QTEST_MAIN(TestAppPicker)
#include "tst_apppicker.moc"
