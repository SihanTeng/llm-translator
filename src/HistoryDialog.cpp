#include "HistoryDialog.h"

#include "HistoryStore.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
QString singleLine(QString text, int maxLength) {
    text = text.simplified();
    if (text.size() > maxLength)
        text = text.left(maxLength) + u"…"_s;
    return text;
}
} // namespace

HistoryDialog::HistoryDialog(HistoryStore *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_filterEdit(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_copyButton(new QPushButton(tr("Copy translation"), this)) {
    setWindowTitle(tr("Translation History"));
    setMinimumSize(520, 420);

    m_filterEdit->setPlaceholderText(tr("Filter…"));
    m_filterEdit->setClearButtonEnabled(true);

    auto *clearButton = new QPushButton(tr("Clear history"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_copyButton);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &HistoryDialog::rebuild);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem *) { copySelected(); });
    connect(m_copyButton, &QPushButton::clicked, this, &HistoryDialog::copySelected);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (m_store->count() == 0)
            return;
        const auto answer = QMessageBox::question(
            this, tr("Clear history"), tr("Delete all %1 history entries?").arg(m_store->count()));
        if (answer == QMessageBox::Yes) {
            m_store->clear();
            rebuild();
        }
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    rebuild();
}

void HistoryDialog::rebuild() {
    const QString filter = m_filterEdit->text().trimmed();
    m_list->clear();
    for (const HistoryEntry &entry : m_store->entries()) {
        if (!filter.isEmpty() && !entry.source.contains(filter, Qt::CaseInsensitive)
            && !entry.translation.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(
            u"%1\n→ %2"_s.arg(singleLine(entry.source, 80), singleLine(entry.translation, 120)));
        item->setData(Qt::UserRole, entry.translation);
        item->setToolTip(QLocale().toString(
            QDateTime::fromSecsSinceEpoch(entry.timestamp), QLocale::ShortFormat));
        m_list->addItem(item);
    }
}

void HistoryDialog::copySelected() {
    const QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    QGuiApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
    m_copyButton->setText(tr("Copied"));
    QTimer::singleShot(1200, this, [this] { m_copyButton->setText(tr("Copy translation")); });
}
