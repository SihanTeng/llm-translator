#include "HistoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

HistoryStore::HistoryStore(QObject *parent)
    : QObject(parent) { }

void HistoryStore::add(const QString &source, const QString &translation) {
    const QString trimmedSource = source.trimmed();
    const QString trimmedTranslation = translation.trimmed();
    if (trimmedSource.isEmpty() || trimmedTranslation.isEmpty())
        return;
    ensureLoaded();
    m_entries.prepend({ QDateTime::currentSecsSinceEpoch(), trimmedSource, trimmedTranslation });
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    save();
}

const QList<HistoryEntry> &HistoryStore::entries() {
    ensureLoaded();
    return m_entries;
}

int HistoryStore::count() {
    ensureLoaded();
    return m_entries.size();
}

void HistoryStore::clear() {
    ensureLoaded();
    m_entries.clear();
    save();
}

void HistoryStore::ensureLoaded() {
    if (m_loaded)
        return;
    m_loaded = true;
    if (m_path.isEmpty()) {
        m_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/history.json"_L1;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const HistoryEntry entry { obj["ts"_L1].toInteger(), obj["source"_L1].toString(),
            obj["translation"_L1].toString() };
        if (!entry.source.isEmpty() && !entry.translation.isEmpty())
            m_entries.append(entry);
    }
}

void HistoryStore::save() const {
    QJsonArray array;
    for (const HistoryEntry &entry : m_entries) {
        array.append(QJsonObject { { "ts"_L1, entry.timestamp }, { "source"_L1, entry.source },
            { "translation"_L1, entry.translation } });
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}
