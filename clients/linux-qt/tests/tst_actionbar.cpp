// Unit test for ActionBar: click emits translateRequested, auto-hide emits
// dismissed, offer() shows the bar.

#include "../src/ActionBar.h"

#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

class TestActionBar : public QObject {
    Q_OBJECT

private slots:
    void clickFlow();
    void dismissedOnHide();
};

void TestActionBar::clickFlow() {
    ActionBar bar;
    QSignalSpy translateSpy(&bar, &ActionBar::translateRequested);
    QSignalSpy dismissedSpy(&bar, &ActionBar::dismissed);

    bar.offer(QPoint(100, 100));
    QVERIFY(bar.isVisible());

    auto *button = bar.findChild<QToolButton *>();
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);

    QCOMPARE(translateSpy.count(), 1);
    QVERIFY(!bar.isVisible());
    QCOMPARE(dismissedSpy.count(), 1);
}

void TestActionBar::dismissedOnHide() {
    ActionBar bar;
    QSignalSpy dismissedSpy(&bar, &ActionBar::dismissed);
    bar.offer(QPoint(100, 100));
    QVERIFY(bar.isVisible());
    bar.hide();
    QCOMPARE(dismissedSpy.count(), 1);
}

QTEST_MAIN(TestActionBar)
#include "tst_actionbar.moc"
