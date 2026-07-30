#pragma once

#include "SettingsDialog.h"
#include <QObject>
#include <QPoint>

class ActionBar;
class Backend;
class PopupWindow;
class SelectionMonitor;
class Speaker;
class Updater;
class QProgressDialog;
class QSystemTrayIcon;

// Wires everything together: selection -> action bar -> (on click) backend
// streaming -> popup, plus the tray menu and settings lifecycle. The actual
// provider traffic, prompts, and history live in the Rust backend (see
// backend/); this class is pure UI-side orchestration.
//
// Selections arrive via two paths:
//  - X11 sessions: SelectionMonitor watches the PRIMARY selection directly.
//  - GNOME Wayland: the companion GNOME Shell extension forwards selections
//    to the TranslateSelection D-Bus slot (Wayland does not let background
//    apps read other apps' selections).
class AppController : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.translator.App")

public:
    explicit AppController(QObject *parent = nullptr);

public slots:
    // Called by the GNOME Shell extension. x/y is the pointer position at
    // selection time; only honored on X11 (Wayland compositors place windows).
    void TranslateSelection(const QString &text, int x, int y);

    // Same as TranslateSelection, plus the sentence the word was found in
    // (for contextual dictionary explanations). Kept separate so older
    // extension versions calling the 3-arg form keep working.
    void TranslateSelectionWithContext(const QString &text, const QString &context, int x, int y);

    // Called by the GNOME Shell extension when it loads/unloads. When
    // enabled, the app renders no Qt UI for D-Bus-triggered translations;
    // the extension renders the action bar and translation panel itself
    // (the only way to position UI at the pointer on GNOME Wayland) and
    // consumes the Translation* signals below instead.
    void SetShellUiEnabled(bool enabled);

    // Called by the GNOME Shell extension's panel Settings button.
    void ShowSettings();

    // Called by the GNOME Shell extension's panel speaker button: speaks the
    // text via the system TTS (spd-say).
    void SpeakText(const QString &text);

    // WM_CLASS names the Shell extension must not offer translations for
    // (from Settings -> Exclude apps).
    [[nodiscard]] QStringList GetExcludedApps() const { return m_settings.excludedApps; }

signals:
    // Forwarded to the D-Bus session bus (ExportAllSignals) for the GNOME
    // Shell extension's translation panel.
    void TranslationToken(const QString &delta);
    void TranslationFinished();
    void TranslationError(const QString &message);
    // Raw JSON payload of a dictionary-mode word explanation, so the Shell
    // extension can render its own structured card.
    void TranslationWordCard(const QString &jsonPayload);
    // Emitted when the app exclusion list changes in Settings; the Shell
    // extension refreshes its cached copy.
    void ExcludedAppsChanged(const QStringList &apps);

private:
    void onSelection(const QString &text);
    void offerTranslation(const QString &text, const QString &context, const QPoint &globalPos);
    void startPendingTranslation();
    void startTranslation(
        const QString &text, const QString &context, const QPoint &globalPos, bool showPopup);
    void openSettings();
    void openHistory();
    void applySettings(const AppSettings &settings);
    void configureBackend();
    void checkForUpdates();
    [[nodiscard]] QString targetLanguageName() const;
    static bool isShortText(const QString &text);

    SelectionMonitor *m_monitor;
    Backend *m_backend;
    PopupWindow *m_popup;
    ActionBar *m_actionBar;
    Updater *m_updater;
    Speaker *m_speaker;
    QProgressDialog *m_updateProgress = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    AppSettings m_settings;
    QString m_pendingText;
    QString m_pendingContext;
    QPoint m_pendingPos;
    bool m_shellUiEnabled = false;
    bool m_jsonMode = false;
    QString m_jsonBuffer;
};
