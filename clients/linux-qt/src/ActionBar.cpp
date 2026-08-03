#include "ActionBar.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QStyleOption>
#include <QTimer>
#include <QToolButton>

#if defined(TRANSLATOR_HAVE_XCB)
#include <xcb/xcb.h>

#include <cstdlib>
#endif

using namespace Qt::StringLiterals;

ActionBar::ActionBar(QWidget *parent)
    : QWidget(parent,
          [] {
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
    , m_autoHide(new QTimer(this)) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Never steal keyboard focus: the user must still be able to Ctrl+C the
    // selection in the source app while the bar is visible.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_X11DoNotAcceptFocus);

    const QIcon translateIcon = QIcon::fromTheme(QStringLiteral("accessories-dictionary"),
        QIcon::fromTheme(QStringLiteral("preferences-desktop-locale")));
    if (translateIcon.isNull())
        m_button->setText(tr("Translate"));
    else {
        m_button->setIcon(translateIcon);
        m_button->setIconSize(QSize(18, 18));
    }
    m_button->setToolTip(tr("Translate"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setStyleSheet("QToolButton { color: white; background: #2d7dff; border-radius: 4px; "
                            "padding: 4px 12px; font-weight: bold; }"
                            "QToolButton:hover { background: #4a90ff; }"_L1);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_button);

    setStyleSheet("ActionBar { background: palette(window); border: 1px solid palette(mid); "
                  "border-radius: 6px; }"_L1);

    m_autoHide->setSingleShot(true);
    m_autoHide->setInterval(kAutoHideMs);
    connect(m_autoHide, &QTimer::timeout, this, &QWidget::hide);

    connect(m_button, &QToolButton::clicked, this, [this] {
        hide();
        emit translateRequested();
    });

#if defined(TRANSLATOR_HAVE_XCB)
    // Click-away dismissal without taking focus: the bar is never activated,
    // so WindowDeactivate never fires. Instead watch _NET_ACTIVE_WINDOW on
    // the root window(s) — a focus change while the bar is visible means the
    // user clicked into another window.
    if (QGuiApplication::platformName() == "xcb"_L1) {
        if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
            xcb_connection_t *connection = x11->connection();
            static const char kActiveWindowName[] = "_NET_ACTIVE_WINDOW";
            const xcb_intern_atom_cookie_t cookie
                = xcb_intern_atom(connection, 0, sizeof(kActiveWindowName) - 1, kActiveWindowName);
            if (xcb_intern_atom_reply_t *reply
                = xcb_intern_atom_reply(connection, cookie, nullptr)) {
                m_activeWindowAtom = reply->atom;
                free(reply);
            }
            const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
            for (xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(connection));
                it.rem; xcb_screen_next(&it))
                xcb_change_window_attributes(connection, it.data->root, XCB_CW_EVENT_MASK, &mask);
            qApp->installNativeEventFilter(this);
        }
    }
#endif
}

void ActionBar::offer(const QPoint &globalPos) {
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

void ActionBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ActionBar::paintEvent(QPaintEvent *event) {
    // Required so the stylesheet background/border on this QWidget subclass
    // actually gets painted.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    QWidget::paintEvent(event);
}

void ActionBar::hideEvent(QHideEvent *event) {
    m_autoHide->stop();
    emit dismissed();
    QWidget::hideEvent(event);
}

bool ActionBar::event(QEvent *event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible())
        hide();
    return QWidget::event(event);
}

bool ActionBar::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(result);
#if defined(TRANSLATOR_HAVE_XCB)
    if (m_activeWindowAtom != 0 && isVisible() && eventType == "xcb_generic_event_t") {
        const auto *event = static_cast<const xcb_generic_event_t *>(message);
        if ((event->response_type & ~0x80) == XCB_PROPERTY_NOTIFY) {
            const auto *notify = reinterpret_cast<const xcb_property_notify_event_t *>(event);
            // The bar itself never takes focus, so any active-window change
            // while it is visible comes from clicking elsewhere.
            if (notify->atom == m_activeWindowAtom)
                hide();
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
