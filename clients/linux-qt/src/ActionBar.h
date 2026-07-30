#pragma once

#include <QPoint>
#include <QWidget>

class QTimer;
class QToolButton;

// Compact action bar shown next to a fresh text selection. Clicking it
// starts the translation; it hides on click-away, Esc, or a timeout.
class ActionBar : public QWidget {
    Q_OBJECT

public:
    explicit ActionBar(QWidget *parent = nullptr);

    // Shows the bar near the given global position and restarts the
    // auto-hide timer.
    void offer(const QPoint &globalPos);

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

    static constexpr int kAutoHideMs = 8000;
};
