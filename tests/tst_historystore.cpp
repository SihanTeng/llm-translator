// Unit tests for HistoryStore: JSON persistence, newest-first ordering, the
// entry cap, and clear(). Each test uses an explicit temp path via setPath().

#include "../src/HistoryStore.h"

#include <QtTest>

class TestHistoryStore : public QObject {
    Q_OBJECT

private slots:
    void addPersistsAcrossInstances();
    void skipsEmptyEntries();
    void capDropsOldest();
    void clearEmpties();
};

void TestHistoryStore::addPersistsAcrossInstances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.json"));

    HistoryStore store;
    store.setPath(path);
    store.add(QStringLiteral("hello"), QStringLiteral("你好"));
    store.add(QStringLiteral("bank"), QStringLiteral("银行"));
    QCOMPARE(store.count(), 2);
    // Newest first.
    QCOMPARE(store.entries().constFirst().source, QStringLiteral("bank"));
    QVERIFY(store.entries().constFirst().timestamp > 0);

    HistoryStore reloaded;
    reloaded.setPath(path);
    QCOMPARE(reloaded.count(), 2);
    QCOMPARE(reloaded.entries().at(0).translation, QStringLiteral("银行"));
    QCOMPARE(reloaded.entries().at(1).source, QStringLiteral("hello"));
    QCOMPARE(reloaded.entries().at(1).translation, QStringLiteral("你好"));
}

void TestHistoryStore::skipsEmptyEntries() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    HistoryStore store;
    store.setPath(dir.filePath(QStringLiteral("history.json")));
    store.add(QString(), QStringLiteral("你好"));
    store.add(QStringLiteral("hello"), QStringLiteral("  "));
    store.add(QStringLiteral("  hello  "), QStringLiteral("你好"));
    QCOMPARE(store.count(), 1);
    // Entries are stored trimmed.
    QCOMPARE(store.entries().constFirst().source, QStringLiteral("hello"));
}

void TestHistoryStore::capDropsOldest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    HistoryStore store;
    store.setPath(dir.filePath(QStringLiteral("history.json")));
    for (int i = 0; i < HistoryStore::kMaxEntries + 10; ++i)
        store.add(QStringLiteral("source-%1").arg(i), QStringLiteral("t-%1").arg(i));
    QCOMPARE(store.count(), HistoryStore::kMaxEntries);
    QCOMPARE(store.entries().constFirst().source,
        QStringLiteral("source-%1").arg(HistoryStore::kMaxEntries + 9));
    QCOMPARE(store.entries().constLast().source, QStringLiteral("source-10"));
}

void TestHistoryStore::clearEmpties() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.json"));

    HistoryStore store;
    store.setPath(path);
    store.add(QStringLiteral("hello"), QStringLiteral("你好"));
    store.clear();
    QCOMPARE(store.count(), 0);

    HistoryStore reloaded;
    reloaded.setPath(path);
    QCOMPARE(reloaded.count(), 0);
}

QTEST_MAIN(TestHistoryStore)
#include "tst_historystore.moc"
