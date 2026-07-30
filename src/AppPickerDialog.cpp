#include "AppPickerDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

QList<InstalledApp> AppPickerDialog::installedApps(const QStringList &dirs) {
    const QStringList roots = dirs.isEmpty()
        ? QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)
        : dirs;
    QList<InstalledApp> apps;
    QSet<QString> seenIds; // first dir wins, like the desktop menu
    for (const QString &root : roots) {
        const QDir dir(root);
        const QStringList files = dir.entryList({ "*.desktop"_L1 }, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QString id = file.left(file.size() - 8); // strip ".desktop"
            if (seenIds.contains(id))
                continue;
            seenIds.insert(id);
            QSettings desktop(dir.filePath(file), QSettings::IniFormat);
            desktop.beginGroup(u"Desktop Entry"_s);
            if (desktop.value(u"Hidden"_s).toBool() || desktop.value(u"NoDisplay"_s).toBool())
                continue;
            InstalledApp app;
            app.name = desktop.value(u"Name"_s).toString();
            if (app.name.isEmpty())
                continue;
            app.iconName = desktop.value(u"Icon"_s).toString();
            const QString wmClass = desktop.value(u"StartupWMClass"_s).toString().trimmed();
            if (!wmClass.isEmpty())
                app.wmClasses.append(wmClass);
            if (!app.wmClasses.contains(id, Qt::CaseInsensitive))
                app.wmClasses.append(id);
            apps.append(app);
        }
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

AppPickerDialog::AppPickerDialog(QWidget *parent)
    : QDialog(parent)
    , m_apps(installedApps())
    , m_filterEdit(new QLineEdit(this))
    , m_list(new QListWidget(this)) {
    setWindowTitle(tr("Choose Apps to Exclude"));
    setMinimumSize(420, 480);

    m_filterEdit->setPlaceholderText(tr("Filter…"));
    m_filterEdit->setClearButtonEnabled(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);

    for (const InstalledApp &app : m_apps) {
        auto *item = new QListWidgetItem(QIcon::fromTheme(app.iconName), app.name, m_list);
        item->setToolTip(app.wmClasses.join(u", "_s));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, app.wmClasses);
    }

    // Filter hides non-matching rows in place, preserving check states.
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString filter = text.trimmed();
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            const bool match = filter.isEmpty()
                || item->text().contains(filter, Qt::CaseInsensitive)
                || item->toolTip().contains(filter, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
    });
}

QStringList AppPickerDialog::selectedWmClasses() const {
    QStringList result;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        const QStringList classes = item->data(Qt::UserRole).toStringList();
        for (const QString &cls : classes) {
            if (!result.contains(cls, Qt::CaseInsensitive))
                result.append(cls);
        }
    }
    return result;
}

void AppPickerDialog::setCheckedWmClasses(const QStringList &classes) {
    QStringList lowered;
    for (const QString &cls : classes)
        lowered.append(cls.toLower());
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const QStringList itemClasses = item->data(Qt::UserRole).toStringList();
        bool any = false;
        for (const QString &cls : itemClasses) {
            if (lowered.contains(cls.toLower())) {
                any = true;
                break;
            }
        }
        item->setCheckState(any ? Qt::Checked : Qt::Unchecked);
    }
}
