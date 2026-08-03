#pragma once

#include <QAbstractNativeEventFilter>
#include <QPoint>
#include <QWidget>

class QTimer;
class QToolButton;

// Compact action bar shown next to a fresh text selection. Clicking it
// starts the translation; it hides on click-away, Esc, or a timeout.
// Click-away cannot rely on focus events (the bar never takes focus, so it
// is never deactivated); on X11 it is detected by watching _NET_ACTIVE_WINDOW
// changes on the root window instead.
class ActionBar : public QWidget, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit ActionBar(QWidget *parent = nullptr);

    // Shows the bar near the given global position and restarts the
    // auto-hide timer.
    void offer(const QPoint &globalPos);

    // QAbstractNativeEventFilter: hides the bar when another window takes
    // focus on X11 (i.e. the user clicked away).
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void translateRequested();
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

private:
    QToolButton *m_button;
    QTimer *m_autoHide;
    // xcb atom for _NET_ACTIVE_WINDOW; 0 when not on X11.
    unsigned int m_activeWindowAtom = 0;

    static constexpr int kAutoHideMs = 8000;
};
