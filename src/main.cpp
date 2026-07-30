#include "AppController.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    // Tests/e2e can isolate settings and data (history) away from the user's
    // real config. (QSettings UserScope follows XDG_CONFIG_HOME on Linux;
    // setPath() on IniFormat does NOT redirect NativeFormat lookups. Set
    // before any Qt object exists, since path resolution is cached early.)
    if (qEnvironmentVariableIsSet("TRANSLATOR_SETTINGS_DIR")) {
        qputenv("XDG_CONFIG_HOME", qgetenv("TRANSLATOR_SETTINGS_DIR"));
        qputenv("XDG_DATA_HOME", qgetenv("TRANSLATOR_SETTINGS_DIR"));
    }

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
