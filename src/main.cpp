#include "AppController.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    // Runs natively on the session's platform (Wayland on modern distros).
    // On X11 sessions selections are monitored via QClipboard; on GNOME
    // Wayland the companion GNOME Shell extension forwards selections over
    // D-Bus. Force XWayland with QT_QPA_PLATFORM=xcb if ever needed.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("translator"));
    QApplication::setOrganizationName(QStringLiteral("translator"));
    QApplication::setQuitOnLastWindowClosed(false);

    AppController controller;
    return QApplication::exec();
}
