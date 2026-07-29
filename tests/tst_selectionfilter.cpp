// Unit tests for SelectionMonitor::isAcceptableSelection.

#include "../src/SelectionMonitor.h"

#include <QtTest>

class TestSelectionFilter : public QObject {
    Q_OBJECT

private slots:
    void filter_data();
    void filter();
};

void TestSelectionFilter::filter_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expected");

    QTest::newRow("empty") << "" << false;
    QTest::newRow("single char") << "a" << false;
    QTest::newRow("two chars") << "ab" << true;
    QTest::newRow("word") << "bank" << true;
    QTest::newRow("sentence") << "The quick brown fox." << true;
    QTest::newRow("exactly max") << QString(4000, 'x') << true;
    QTest::newRow("over max") << QString(4001, 'x') << false;
}

void TestSelectionFilter::filter() {
    QFETCH(QString, text);
    QFETCH(bool, expected);
    QCOMPARE(SelectionMonitor::isAcceptableSelection(text), expected);
}

QTEST_MAIN(TestSelectionFilter)
#include "tst_selectionfilter.moc"
