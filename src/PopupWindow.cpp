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
    setMinimumWidth(280);
    setMaximumWidth(460);

    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setStyleSheet("color: palette(mid); font-size: 11px;"_L1);

    m_resultView->setFrameShape(QFrame::NoFrame);
    m_resultView->setOpenExternalLinks(false);
    m_resultView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultView->setSizeAdjustPolicy(QTextBrowser::AdjustToContents);
    m_resultView->setMaximumHeight(320);

    m_copyButton->setText(tr("Copy"));
    m_copyButton->setAutoRaise(true);
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setAutoRaise(true);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(m_copyButton);
    header->addStretch(1);
    header->addWidget(m_closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 10);
    layout->addLayout(header);
    layout->addWidget(m_sourceLabel);
    layout->addWidget(m_resultView, 1);

    setStyleSheet("PopupWindow { background: palette(base); border: 1px solid palette(mid); "
                  "border-radius: 6px; }"_L1);

    connect(m_copyButton, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_result, QClipboard::Clipboard);
        m_copyButton->setText(tr("Copied"));
        QTimer::singleShot(1200, this, [this] { m_copyButton->setText(tr("Copy")); });
    });
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);
}

void PopupWindow::startTranslation(const QString &sourceText, const QPoint &globalPos)
{
    m_result.clear();
    m_resultView->setPlainText(tr("Translating…"));

    QString source = sourceText.simplified();
    if (source.size() > 140)
        source = source.left(140) + "…"_L1;
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
}

void PopupWindow::showError(const QString &message)
{
    m_resultView->setPlainText(message);
}

void PopupWindow::placeNear(const QPoint &globalPos)
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
