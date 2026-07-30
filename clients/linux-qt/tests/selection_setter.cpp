// Smoke-test helper: owns the X11 PRIMARY selection with the given text for
// a few seconds, so the translator's SelectionMonitor can pick it up.
//
// Usage: selection_setter <text> [holdSeconds]

#include <QApplication>
#include <QClipboard>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    if (argc < 2) {
        qWarning("usage: %s <text> [holdSeconds]", argv[0]);
        return 2;
    }

    QGuiApplication::clipboard()->setText(QString::fromLocal8Bit(argv[1]), QClipboard::Selection);
    QTextStream(stdout) << "PRIMARY selection set" << '\n';

    const int holdSeconds = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 8;
    QTimer::singleShot(holdSeconds * 1000, &app, &QCoreApplication::quit);
    return QApplication::exec();
}
