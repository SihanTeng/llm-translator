// UNVERIFIED on this machine (GNOME; hyprctl/swaymsg unavailable).

#include "CursorPosition.h"

#include <functional>

#include <QCursor>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>

static QPoint runHelper(const QString &program, const QStringList &arguments,
                        std::function<QPoint(const QByteArray &)> parse)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(500) || process.exitCode() != 0)
        return QPoint();
    return parse(process.readAllStandardOutput());
}

QPoint globalCursorPosition()
{
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        return QCursor::pos();

    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");

    if (desktop.contains("Hyprland")) {
        return runHelper(QStringLiteral("hyprctl"), {QStringLiteral("cursorpos")},
                         [](const QByteArray &out) {
            // Format: "x, y"
            const QList<QByteArray> parts = out.trimmed().split(',');
            if (parts.size() != 2)
                return QPoint();
            return QPoint(parts[0].toInt(), parts[1].toInt());
        });
    }

    if (desktop.contains("sway")) {
        return runHelper(QStringLiteral("swaymsg"), {QStringLiteral("-t"), QStringLiteral("get_seats")},
                         [](const QByteArray &out) {
            // Not part of the stable sway IPC output schema; best effort.
            const QJsonArray seats = QJsonDocument::fromJson(out).array();
            if (seats.isEmpty())
                return QPoint();
            const QJsonObject cursor = seats[0].toObject()["cursor"].toObject();
            return QPoint(cursor["x"].toInt(), cursor["y"].toInt());
        });
    }

    return QPoint();
}
