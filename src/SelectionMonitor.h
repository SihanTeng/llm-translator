#pragma once

#include <QObject>
#include <QString>

class QClipboard;
class QTimer;

// Watches the X11 PRIMARY selection ("highlight with mouse") and emits
// selectionReady() for new, meaningful text. Changes are debounced so a
// single drag-select produces one translation request.
class SelectionMonitor : public QObject {
    Q_OBJECT

public:
    explicit SelectionMonitor(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    // Clears the duplicate-selection memory so the same text can trigger
    // again (e.g. after the action bar was dismissed).
    void resetLastEmitted() { m_lastEmitted.clear(); }

signals:
    void selectionReady(const QString &text);
    // Emitted when the PRIMARY selection becomes empty (user clicked away /
    // deselected); lets the UI dismiss any selection-dependent chrome.
    void selectionCleared();

private:
    void onSelectionChanged();

    QClipboard *m_clipboard;
    QTimer *m_debounce;
    QString m_pending;
    QString m_lastEmitted;
    bool m_enabled = true;

    static constexpr int kDebounceMs = 300;
    static constexpr int kMinLength = 2;
    static constexpr int kMaxLength = 4000;
};
