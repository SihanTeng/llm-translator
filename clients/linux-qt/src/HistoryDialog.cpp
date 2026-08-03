#include "HistoryDialog.h"

#include "Backend.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

// Roles for the custom row paint.
constexpr int kRoleTranslation = Qt::UserRole; // full text → clipboard
constexpr int kRoleSource = Qt::UserRole + 1;
constexpr int kRoleTime = Qt::UserRole + 2;

// Index-First row: source (muted) + time (meta, top-right) over translation
// (primary). Density from type weight, not chrome. Cobalt: 6px selection radius.
class HistoryItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QRect r = opt.rect.adjusted(4, 2, -4, -2);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(opt.palette.color(QPalette::Highlight));
            painter->drawRoundedRect(r, 6, 6);
        } else if (hovered) {
            QColor wash = opt.palette.color(QPalette::Midlight);
            wash.setAlpha(160);
            painter->setPen(Qt::NoPen);
            painter->setBrush(wash);
            painter->drawRoundedRect(r, 6, 6);
        }

        const QString source = index.data(kRoleSource).toString().simplified();
        const QString translation = index.data(kRoleTranslation).toString().simplified();
        const QString time = index.data(kRoleTime).toString();

        const QColor ink = selected ? opt.palette.color(QPalette::HighlightedText)
                                    : opt.palette.color(QPalette::Text);
        QColor muted = selected ? ink : opt.palette.color(QPalette::PlaceholderText);
        if (!selected)
            muted.setAlpha(200);

        const int padX = 14;
        const int padY = 10;
        const QRect content = r.adjusted(padX, padY, -padX, -padY);

        QFont timeFont = opt.font;
        timeFont.setPointSizeF(qMax(9.0, opt.font.pointSizeF() - 1.5));
        timeFont.setLetterSpacing(QFont::PercentageSpacing, 104);
        QFontMetrics timeFm(timeFont);
        const int timeW = time.isEmpty() ? 0 : timeFm.horizontalAdvance(time) + 8;
        const QRect timeRect(content.right() - timeW + 1, content.top(), timeW, timeFm.height());

        QFont sourceFont = opt.font;
        sourceFont.setPointSizeF(qMax(10.0, opt.font.pointSizeF() - 1.0));
        QFontMetrics sourceFm(sourceFont);
        const QRect sourceRect(
            content.left(), content.top(), qMax(0, content.width() - timeW - 8), sourceFm.height());

        painter->setFont(sourceFont);
        painter->setPen(muted);
        painter->drawText(sourceRect, Qt::AlignLeft | Qt::AlignVCenter,
            sourceFm.elidedText(source, Qt::ElideRight, sourceRect.width()));

        if (!time.isEmpty()) {
            painter->setFont(timeFont);
            painter->setPen(muted);
            painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, time);
        }

        QFont bodyFont = opt.font;
        bodyFont.setPointSizeF(opt.font.pointSizeF() + 0.5);
        bodyFont.setWeight(QFont::Medium);
        QFontMetrics bodyFm(bodyFont);
        const QRect bodyRect(
            content.left(), sourceRect.bottom() + 4, content.width(), bodyFm.height());

        painter->setFont(bodyFont);
        painter->setPen(ink);
        painter->drawText(bodyRect, Qt::AlignLeft | Qt::AlignVCenter,
            bodyFm.elidedText(translation, Qt::ElideRight, bodyRect.width()));

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        const int h = qMax(56, option.fontMetrics.height() * 2 + 28);
        return QSize(option.rect.width(), h);
    }
};

} // namespace

HistoryDialog::HistoryDialog(Backend *backend, QWidget *parent)
    : QDialog(parent)
    , m_backend(backend)
    , m_titleLabel(new QLabel(tr("History"), this))
    , m_metaLabel(new QLabel(this))
    , m_filterEdit(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_emptyLabel(new QLabel(this))
    , m_copyButton(new QPushButton(tr("Copy translation"), this))
    , m_clearButton(new QPushButton(tr("Clear"), this)) {
    setWindowTitle(tr("Translation History"));
    setMinimumSize(560, 480);
    resize(620, 560);

    // Cobalt-on-system: palette for paper/ink (light & dark), link as the one
    // signal accent. Hairline borders, 6px radii, no drop shadows.
    // Hallmark · genre: modern-minimal · macrostructure: Index-First
    // theme: Cobalt (palette-mapped) · tone: utilitarian · enrichment: none
    setStyleSheet(R"(
        HistoryDialog {
            background: palette(window);
        }
        QLabel#historyTitle {
            font-size: 20px;
            font-weight: 600;
            color: palette(window-text);
            letter-spacing: -0.3px;
        }
        QLabel#historyMeta {
            font-size: 11px;
            font-weight: 500;
            color: palette(mid);
            letter-spacing: 0.4px;
        }
        QLineEdit#historyFilter {
            padding: 9px 12px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
            selection-background-color: palette(highlight);
            font-size: 13px;
        }
        QLineEdit#historyFilter:focus {
            border: 1px solid palette(link);
        }
        QListWidget#historyList {
            border: 1px solid palette(mid);
            border-radius: 8px;
            background: palette(base);
            outline: none;
            padding: 4px 0;
        }
        QListWidget#historyList::item {
            border: none;
            padding: 0;
            background: transparent;
        }
        QListWidget#historyList::item:selected {
            background: transparent;
        }
        QLabel#historyEmpty {
            color: palette(mid);
            font-size: 13px;
            padding: 32px 24px;
        }
        QPushButton {
            padding: 7px 16px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(button);
            font-size: 13px;
        }
        QPushButton:hover { background: palette(midlight); }
        QPushButton:disabled {
            color: palette(mid);
            background: palette(button);
        }
        QPushButton:default {
            background: palette(link);
            color: white;
            border: none;
            font-weight: 500;
        }
        QPushButton:default:hover { background: palette(highlight); }
        QPushButton:default:disabled {
            background: palette(mid);
            color: palette(base);
            border: none;
        }
        QPushButton#historyClear {
            border: none;
            background: transparent;
            color: palette(mid);
            padding: 7px 10px;
        }
        QPushButton#historyClear:hover {
            color: palette(window-text);
            background: palette(midlight);
        }
        QPushButton#historyClear:disabled {
            color: palette(mid);
            background: transparent;
        }
    )"_L1);

    m_titleLabel->setObjectName(u"historyTitle"_s);
    m_metaLabel->setObjectName(u"historyMeta"_s);
    m_metaLabel->setText(tr("LOCAL ARCHIVE"));

    m_filterEdit->setObjectName(u"historyFilter"_s);
    m_filterEdit->setPlaceholderText(tr("Search source or translation…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setFocus();

    m_list->setObjectName(u"historyList"_s);
    m_list->setItemDelegate(new HistoryItemDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(false);
    m_list->setSpacing(0);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAttribute(Qt::WA_Hover, true);

    m_emptyLabel->setObjectName(u"historyEmpty"_s);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyLabel->setParent(m_list->viewport());
    m_emptyLabel->hide();

    m_copyButton->setDefault(true);
    m_copyButton->setEnabled(false);
    m_clearButton->setObjectName(u"historyClear"_s);
    m_clearButton->setToolTip(tr("Delete every saved translation"));
    m_clearButton->setCursor(Qt::PointingHandCursor);

    auto *closeButton = new QPushButton(tr("Close"), this);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    header->addWidget(m_titleLabel, 0, Qt::AlignBottom);
    header->addStretch(1);
    header->addWidget(m_metaLabel, 0, Qt::AlignBottom);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 4, 0, 0);
    footer->setSpacing(8);
    footer->addWidget(m_clearButton, 0, Qt::AlignLeft);
    footer->addStretch(1);
    footer->addWidget(m_copyButton);
    footer->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    // 4pt scale: 20 / 18 / 16 outer rhythm (space-lg / space-md-ish).
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addSpacing(14);
    layout->addWidget(m_filterEdit);
    layout->addSpacing(12);
    layout->addWidget(m_list, 1);
    layout->addSpacing(14);
    layout->addLayout(footer);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &HistoryDialog::rebuild);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem *) { copySelected(); });
    connect(m_list, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem *, QListWidgetItem *) { updateChrome(); });
    connect(m_copyButton, &QPushButton::clicked, this, &HistoryDialog::copySelected);
    connect(m_clearButton, &QPushButton::clicked, this, [this] {
        if (m_totalEntries == 0)
            return;
        const auto answer = QMessageBox::question(
            this, tr("Clear history"), tr("Delete all %1 history entries?").arg(m_totalEntries));
        if (answer == QMessageBox::Yes) {
            m_backend->historyClear();
            rebuild();
        }
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    rebuild();
}

void HistoryDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    placeEmptyLabel();
}

void HistoryDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    placeEmptyLabel();
}

void HistoryDialog::placeEmptyLabel() {
    if (!m_emptyLabel->isHidden())
        m_emptyLabel->setGeometry(m_list->viewport()->rect());
}

void HistoryDialog::rebuild() {
    const QString filter = m_filterEdit->text().trimmed();
    m_list->clear();
    const QJsonArray entries = QJsonDocument::fromJson(m_backend->historyJson().toUtf8()).array();
    m_totalEntries = entries.size();
    m_visibleEntries = 0;

    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString source = entry["source"_L1].toString();
        const QString translation = entry["translation"_L1].toString();
        if (!filter.isEmpty() && !source.contains(filter, Qt::CaseInsensitive)
            && !translation.contains(filter, Qt::CaseInsensitive))
            continue;

        auto *item = new QListWidgetItem(m_list);
        item->setData(kRoleSource, source);
        item->setData(kRoleTranslation, translation);

        const qint64 ts = entry["ts"_L1].toInteger();
        QString timeLabel;
        if (ts > 0) {
            const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts);
            const QDateTime now = QDateTime::currentDateTime();
            if (dt.date() == now.date())
                timeLabel = QLocale().toString(dt.time(), QLocale::ShortFormat);
            else if (dt.date().year() == now.date().year())
                timeLabel = QLocale().toString(dt, u"MMM d"_s);
            else
                timeLabel = QLocale().toString(dt.date(), QLocale::ShortFormat);
        }
        item->setData(kRoleTime, timeLabel);
        item->setToolTip(u"%1\n→ %2\n%3"_s.arg(source, translation,
            ts > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(ts), QLocale::LongFormat)
                   : QString()));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ++m_visibleEntries;
    }

    if (m_list->count() > 0 && m_list->currentRow() < 0)
        m_list->setCurrentRow(0);

    updateChrome();
}

void HistoryDialog::updateChrome() {
    const bool hasSelection = m_list->currentItem() != nullptr;
    m_copyButton->setEnabled(hasSelection);
    m_clearButton->setEnabled(m_totalEntries > 0);

    if (m_totalEntries == 0) {
        m_metaLabel->setText(tr("EMPTY"));
        m_emptyLabel->setText(tr("No translations yet.\nSelect text anywhere to start."));
        m_emptyLabel->show();
        placeEmptyLabel();
    } else if (m_visibleEntries == 0) {
        m_metaLabel->setText(tr("0 OF %1").arg(m_totalEntries));
        m_emptyLabel->setText(tr("No matches for “%1”.").arg(m_filterEdit->text().trimmed()));
        m_emptyLabel->show();
        placeEmptyLabel();
    } else if (!m_filterEdit->text().trimmed().isEmpty()) {
        m_metaLabel->setText(tr("%1 OF %2").arg(m_visibleEntries).arg(m_totalEntries));
        m_emptyLabel->hide();
    } else {
        m_metaLabel->setText(tr("%1 SAVED").arg(m_totalEntries));
        m_emptyLabel->hide();
    }
}

void HistoryDialog::copySelected() {
    const QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    QGuiApplication::clipboard()->setText(item->data(kRoleTranslation).toString());
    // Silent success: brief label flip, no toast.
    m_copyButton->setText(tr("Copied"));
    QTimer::singleShot(1200, this, [this] { m_copyButton->setText(tr("Copy translation")); });
}
