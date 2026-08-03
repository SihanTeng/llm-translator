#include "AppController.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QIcon>
#include <QTimer>
#include <cstdio>

namespace {
constexpr auto kService = "org.translator.App";
constexpr auto kPath = "/org/translator/App";
constexpr auto kInterface = "org.translator.App";

bool serviceIsRunning() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusConnectionInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(QString::fromLatin1(kService));
}

// Forward a control-plane call to an already-running instance. Returns true
// when the call was delivered (including a remote method error response).
bool callRunning(const QString &method, const QVariantList &args = { }) {
    QDBusInterface remote(QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QDBusConnection::sessionBus());
    if (!remote.isValid()) {
        std::fprintf(stderr, "translator: cannot reach %s (%s)\n", kService,
            qPrintable(remote.lastError().message()));
        return false;
    }
    const QDBusMessage reply = args.isEmpty()
        ? remote.call(method)
        : remote.callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        std::fprintf(stderr, "translator: %s failed: %s\n", qPrintable(method),
            qPrintable(reply.errorMessage()));
        return false;
    }
    return true;
}
} // namespace

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
    QApplication::setApplicationVersion(QStringLiteral(TRANSLATOR_VERSION));
    QApplication::setOrganizationName(QStringLiteral("translator"));
    QApplication::setQuitOnLastWindowClosed(false);
    // Theme icon when installed; embedded PNG when running from the build tree.
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("translator"), QIcon(QStringLiteral(":/translator.png"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Select-text popup translator (LLM). "
                       "Control plane for hotkeys: --translate / --translate-clipboard."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption translateOpt({ QStringLiteral("t"), QStringLiteral("translate") },
        QStringLiteral("Translate TEXT immediately (no action bar)."), QStringLiteral("text"));
    QCommandLineOption clipboardOpt({ QStringLiteral("c"), QStringLiteral("translate-clipboard") },
        QStringLiteral("Translate the clipboard (Ctrl+C buffer)."));
    QCommandLineOption settingsOpt(
        QStringLiteral("show-settings"), QStringLiteral("Open the Settings dialog."));
    QCommandLineOption cancelOpt(
        QStringLiteral("cancel"), QStringLiteral("Cancel any in-flight translation."));
    parser.addOption(translateOpt);
    parser.addOption(clipboardOpt);
    parser.addOption(settingsOpt);
    parser.addOption(cancelOpt);
    parser.process(app);

    const bool wantTranslate = parser.isSet(translateOpt);
    const bool wantClipboard = parser.isSet(clipboardOpt);
    const bool wantSettings = parser.isSet(settingsOpt);
    const bool wantCancel = parser.isSet(cancelOpt);
    const bool remoteOnly = wantTranslate || wantClipboard || wantSettings || wantCancel;

    // If a primary instance already owns the bus name, forward CLI verbs and
    // exit — single-instance control plane (Crow/Pot pattern).
    if (remoteOnly && serviceIsRunning()) {
        bool ok = true;
        if (wantCancel)
            ok = callRunning(QStringLiteral("CancelTranslation")) && ok;
        if (wantClipboard)
            ok = callRunning(QStringLiteral("TranslateClipboard")) && ok;
        if (wantTranslate) {
            ok = callRunning(QStringLiteral("TranslateText"), { parser.value(translateOpt) }) && ok;
        }
        if (wantSettings)
            ok = callRunning(QStringLiteral("ShowSettings")) && ok;
        return ok ? 0 : 1;
    }

    AppController controller;

    // We are the primary instance. Honour CLI verbs after the event loop
    // starts so D-Bus registration and settings load have completed.
    if (wantCancel)
        QTimer::singleShot(0, &controller, &AppController::CancelTranslation);
    if (wantClipboard)
        QTimer::singleShot(0, &controller, &AppController::TranslateClipboard);
    if (wantTranslate) {
        const QString text = parser.value(translateOpt);
        QTimer::singleShot(0, &controller, [text, &controller] { controller.TranslateText(text); });
    }
    if (wantSettings)
        QTimer::singleShot(0, &controller, &AppController::ShowSettings);

    return QApplication::exec();
}
