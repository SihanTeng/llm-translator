#pragma once

#include <QList>
#include <QObject>
#include <QString>

// One completed translation: the selected text and its result, with a Unix
// timestamp (seconds).
struct HistoryEntry {
    qint64 timestamp = 0;
    QString source;
    QString translation;
};

// Persists translation history to a JSON file
// (~/.local/share/translator/history.json by default), newest first, capped
// at kMaxEntries so the whole file stays cheap to rewrite on each add.
class HistoryStore : public QObject {
    Q_OBJECT

public:
    explicit HistoryStore(QObject *parent = nullptr);

    // Overrides the storage path (tests); call before the first use.
    void setPath(const QString &path) { m_path = path; }

    void add(const QString &source, const QString &translation);
    [[nodiscard]] const QList<HistoryEntry> &entries();
    [[nodiscard]] int count();
    void clear();

    static constexpr int kMaxEntries = 500;

private:
    void ensureLoaded();
    void save() const;

    QString m_path;
    QList<HistoryEntry> m_entries;
    bool m_loaded = false;
};
