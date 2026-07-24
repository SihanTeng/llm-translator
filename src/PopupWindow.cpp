#include "PopupWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QStyleOption>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_sourceLabel(new QLabel(this))
    , m_resultView(new QTextBrowser(this))
    , m_copyButton(new QToolButton(this))
    , m_closeButton(new QToolButton(this))
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Width/height are sized per screen in placeNear() (responsive):
    // generous fraction of the available geometry, height tracks content.

    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setStyleSheet("color: palette(mid); font-size: 11px;"_L1);

    m_resultView->setFrameShape(QFrame::NoFrame);
    m_resultView->setOpenExternalLinks(false);
    m_resultView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultView->setSizeAdjustPolicy(QTextBrowser::AdjustToContents);
    m_resultView->setStyleSheet(
        "QTextBrowser { background: transparent; color: palette(text); font-size: 14px; }"_L1);

    const QIcon copyIcon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    if (copyIcon.isNull())
        m_copyButton->setText(tr("Copy"));
    else
        m_copyButton->setIcon(copyIcon);
    m_copyButton->setToolTip(tr("Copy"));
    m_copyButton->setAutoRaise(true);
    m_copyButton->setFixedSize(28, 28);
    m_copyButton->setIconSize(QSize(16, 16));
    const QIcon closeIcon = QIcon::fromTheme(QStringLiteral("window-close"));
    if (closeIcon.isNull())
        m_closeButton->setText(QStringLiteral("×"));
    else
        m_closeButton->setIcon(closeIcon);
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setFixedSize(28, 28);
    m_closeButton->setIconSize(QSize(16, 16));
    const QString headerButtonStyle =
        "QToolButton { color: palette(mid); padding: 2px 8px; border-radius: 4px; }"
        "QToolButton:hover { color: palette(text); background: palette(midlight); }"_L1;
    m_copyButton->setStyleSheet(headerButtonStyle);
    m_closeButton->setStyleSheet(headerButtonStyle);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(m_copyButton);
    header->addStretch(1);
    header->addWidget(m_closeButton);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    auto *settingsButton = new QToolButton(this);
    const QIcon settingsIcon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (settingsIcon.isNull())
        settingsButton->setText(tr("Settings"));
    else
        settingsButton->setIcon(settingsIcon);
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setAutoRaise(true);
    settingsButton->setStyleSheet(headerButtonStyle);
    footer->addWidget(settingsButton);
    footer->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(5);
    layout->addLayout(header);
    layout->addWidget(m_sourceLabel);
    layout->addWidget(m_resultView, 1);
    layout->addLayout(footer);

    // System-native chrome: follow the desktop palette (Adwaita light/dark)
    // instead of a hardcoded theme.
    setStyleSheet("PopupWindow { background: palette(base); border: 1px solid palette(mid); "
                  "border-radius: 8px; }"_L1);

    connect(m_copyButton, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_result, QClipboard::Clipboard);
        const QIcon okIcon = QIcon::fromTheme(QStringLiteral("object-select"),
                                              QIcon::fromTheme(QStringLiteral("emblem-ok")));
        if (!okIcon.isNull() && !m_copyButton->icon().isNull()) {
            const QIcon original = m_copyButton->icon();
            m_copyButton->setIcon(okIcon);
            QTimer::singleShot(1200, this, [this, original] { m_copyButton->setIcon(original); });
        } else {
            m_copyButton->setText(tr("Copied"));
            QTimer::singleShot(1200, this, [this] { m_copyButton->setText(tr("Copy")); });
        }
    });
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);
    connect(settingsButton, &QToolButton::clicked, this, &PopupWindow::settingsRequested);
}

void PopupWindow::startTranslation(const QString &sourceText, const QPoint &globalPos)
{
    m_result.clear();
    m_resultView->setPlainText(tr("Translating…"));

    QString source = sourceText.simplified();
    if (source.size() > 140)
        source = source.left(140) + u"…"_s;
    m_sourceLabel->setText(source);

    placeNear(globalPos);
    show();
    raise();
    activateWindow();
}

void PopupWindow::appendToken(const QString &delta)
{
    if (m_result.isEmpty())
        m_resultView->clear();
    m_result += delta;
    m_resultView->setPlainText(m_result);
    ensureOnScreen();
}

void PopupWindow::setResult(const QString &html)
{
    m_resultView->setHtml(html);
    // Keep the plain-text form for the Copy button.
    m_result = m_resultView->toPlainText();
    ensureOnScreen();
}

void PopupWindow::showError(const QString &message)
{
    m_resultView->setPlainText(message);
    ensureOnScreen();
}

void PopupWindow::placeNear(const QPoint &globalPos)
{
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();

    // Responsive sizing: narrow column (half of the previous 35% width),
    // height follows the content up to a generous cap (3x the previous
    // half-screen cap, limited to the screen itself).
    const int targetWidth = qBound(available.width() * 175 / 1000, 210, 360);
    setFixedWidth(targetWidth);
    const int maxHeight = qMin(available.height() * 3 / 2, available.height() - 40);
    m_resultView->setMaximumHeight(qMax(240, maxHeight));

    adjustSize();

    QPoint pos = globalPos + QPoint(12, 16);
    if (pos.x() + width() > available.right())
        pos.setX(available.right() - width());
    if (pos.y() + height() > available.bottom())
        pos.setY(globalPos.y() - height() - 8);
    move(pos);
}

void PopupWindow::ensureOnScreen()
{
    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();

    // Height grows while tokens stream in; keep the frame inside the screen.
    QPoint p = pos();
    if (p.x() + width() > available.right())
        p.setX(available.right() - width());
    if (p.y() + height() > available.bottom())
        p.setY(available.bottom() - height());
    if (p.x() < available.left())
        p.setX(available.left());
    if (p.y() < available.top())
        p.setY(available.top());
    if (p != pos())
        move(p);
}

void PopupWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PopupWindow::paintEvent(QPaintEvent *event)
{
    // Required so the stylesheet background/border on this QWidget subclass
    // actually gets painted.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    QWidget::paintEvent(event);
}

bool PopupWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && isVisible())
        hide();
    return QWidget::event(event);
}
