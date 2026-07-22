#pragma once

#include <QObject>
#include <QPoint>
#include "SettingsDialog.h"

class ActionBar;
class DeepSeekClient;
class PopupWindow;
class SelectionMonitor;
class QSystemTrayIcon;

// Wires everything together: selection -> action bar -> (on click) DeepSeek
// streaming -> popup, plus the tray menu and settings lifecycle.
//
// Selections arrive via two paths:
//  - X11 sessions: SelectionMonitor watches the PRIMARY selection directly.
//  - GNOME Wayland: the companion GNOME Shell extension forwards selections
//    to the TranslateSelection D-Bus slot (Wayland does not let background
//    apps read other apps' selections).
class AppController : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.translator.App")

public:
    explicit AppController(QObject *parent = nullptr);

public slots:
    // Called by the GNOME Shell extension. x/y is the pointer position at
    // selection time; only honored on X11 (Wayland compositors place windows).
    void TranslateSelection(const QString &text, int x, int y);

private:
    void onSelection(const QString &text);
    void offerTranslation(const QString &text, const QPoint &globalPos);
    void startPendingTranslation();
    void openSettings();
    void applySettings(const AppSettings &settings);
    [[nodiscard]] QString buildSystemPrompt() const;

    SelectionMonitor *m_monitor;
    DeepSeekClient *m_client;
    PopupWindow *m_popup;
    ActionBar *m_actionBar;
    QSystemTrayIcon *m_tray = nullptr;
    AppSettings m_settings;
    QString m_pendingText;
    QPoint m_pendingPos;
};
