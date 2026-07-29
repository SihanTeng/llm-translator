// Smoke test for the OTA updater: unit-checks the semver compare, then runs
// a live check() against the GitHub releases API. Exit 0 = all good.

#include "../src/Updater.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    struct {
        const char *latest;
        const char *current;
        bool expected;
    } cases[] = {
        {"v0.1.3", "0.1.2", true},
        {"0.1.2", "0.1.2", false},
        {"0.1.2", "0.1.3", false},
        {"v1.0.0", "0.9.9", true},
        {"0.2", "0.1.9", true},
        {"0.2", "0.2.0", false},
    };
    for (const auto &c : cases) {
        if (Updater::isNewer(c.latest, c.current) != c.expected) {
            QTextStream(stderr) << "FAIL isNewer(" << c.latest << ", " << c.current << ")\n";
            return 1;
        }
    }
    QTextStream(stdout) << "isNewer: all cases pass\n";

    Updater updater;
    QObject::connect(&updater, &Updater::updateAvailable, &app,
        [](const QString &version, const QString &url) {
            QTextStream(stdout) << "updateAvailable: " << version << " " << url << "\n";
            QCoreApplication::exit(url.isEmpty() ? 1 : 0);
        });
    QObject::connect(&updater, &Updater::upToDate, &app, [] {
        QTextStream(stdout) << "upToDate (running latest)\n";
        QCoreApplication::exit(0);
    });
    QObject::connect(&updater, &Updater::failed, &app, [](const QString &message) {
        QTextStream(stderr) << "FAIL: " << message << "\n";
        QCoreApplication::exit(1);
    });

    updater.check();
    QTimer::singleShot(15000, &app, [] { QCoreApplication::exit(2); });
    return QCoreApplication::exec();
}
