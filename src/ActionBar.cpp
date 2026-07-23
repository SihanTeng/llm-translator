#include "ActionBar.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleOption>
#include <QTimer>
#include <QToolButton>

using namespace Qt::StringLiterals;

ActionBar::ActionBar(QWidget *parent)
    : QWidget(parent, [] {
        // On X11 a normal window that appears without taking focus makes
        // GNOME Shell post a "<app> is ready" notification. A tooltip-type
        // window (override-redirect) avoids that; the bar never takes focus
        // anyway. On Wayland, window placement/typing is the compositor's
        // business, so keep a plain frameless window there.
        Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint
                              | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
        if (QGuiApplication::platformName() == "xcb"_L1)
            flags = Qt::ToolTip | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
        return flags;
    }())
    , m_button(new QToolButton(this))
    , m_autoHide(new QTimer(this))
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Never steal keyboard focus: the user must still be able to Ctrl+C the
    // selection in the source app while the bar is visible.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_X11DoNotAcceptFocus);

    m_button->setText(tr("Translate"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setStyleSheet(
        "QToolButton { color: white; background: #2d7dff; border-radius: 4px; "
        "padding: 4px 12px; font-weight: bold; }"
        "QToolButton:hover { background: #4a90ff; }"_L1);

    auto *settingsButton = new QToolButton(this);
    const QIcon settingsIcon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (settingsIcon.isNull())
        settingsButton->setText(QStringLiteral("⚙"));
    else
        settingsButton->setIcon(settingsIcon);
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setAutoRaise(true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_button);
    layout->addWidget(settingsButton);

    setStyleSheet("ActionBar { background: palette(window); border: 1px solid palette(mid); "
                  "border-radius: 6px; }"_L1);

    m_autoHide->setSingleShot(true);
    m_autoHide->setInterval(kAutoHideMs);
    connect(m_autoHide, &QTimer::timeout, this, &QWidget::hide);

    connect(m_button, &QToolButton::clicked, this, [this] {
        hide();
        emit translateRequested();
    });
    connect(settingsButton, &QToolButton::clicked, this, [this] {
        hide();
        emit settingsRequested();
    });
}

void ActionBar::offer(const QPoint &globalPos)
{
    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();

    QPoint pos = globalPos + QPoint(12, 16);
    if (pos.x() + width() > available.right())
        pos.setX(available.right() - width());
    if (pos.y() + height() > available.bottom())
        pos.setY(globalPos.y() - height() - 8);
    move(pos);

    m_autoHide->start();
    show();
    raise();
}

void ActionBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ActionBar::paintEvent(QPaintEvent *event)
{
    // Required so the stylesheet background/border on this QWidget subclass
    // actually gets painted.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    QWidget::paintEvent(event);
}

void ActionBar::hideEvent(QHideEvent *event)
{
    m_autoHide->stop();
    emit dismissed();
    QWidget::hideEvent(event);
}

bool ActionBar::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && isVisible())
        hide();
    return QWidget::event(event);
}
