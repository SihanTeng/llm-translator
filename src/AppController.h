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

    // Called by the GNOME Shell extension when it loads/unloads. When
    // enabled, the app renders no Qt UI for D-Bus-triggered translations;
    // the extension renders the action bar and translation panel itself
    // (the only way to position UI at the pointer on GNOME Wayland) and
    // consumes the Translation* signals below instead.
    void SetShellUiEnabled(bool enabled);

    // Called by the GNOME Shell extension's panel Settings button.
    void ShowSettings();

signals:
    // Forwarded to the D-Bus session bus (ExportAllSignals) for the GNOME
    // Shell extension's translation panel.
    void TranslationToken(const QString &delta);
    void TranslationFinished();
    void TranslationError(const QString &message);

private:
    void onSelection(const QString &text);
    void offerTranslation(const QString &text, const QPoint &globalPos);
    void startPendingTranslation();
    void startTranslation(const QString &text, const QPoint &globalPos, bool showPopup);
    void openSettings();
    void applySettings(const AppSettings &settings);
    [[nodiscard]] QString buildSystemPrompt(bool wordMode) const;
    [[nodiscard]] QString formatWordResult(const QString &jsonPayload) const;
    static bool isSingleWord(const QString &text);

    SelectionMonitor *m_monitor;
    DeepSeekClient *m_client;
    PopupWindow *m_popup;
    ActionBar *m_actionBar;
    QSystemTrayIcon *m_tray = nullptr;
    AppSettings m_settings;
    QString m_pendingText;
    QPoint m_pendingPos;
    bool m_shellUiEnabled = false;
    bool m_wordMode = false;
    QString m_wordBuffer;
};
