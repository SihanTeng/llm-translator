// Smoke test: clicking the ActionBar's button emits translateRequested()
// exactly once and hides the bar; auto-hide emits dismissed().

#include "../src/ActionBar.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    ActionBar bar;
    QSignalSpy translateSpy(&bar, &ActionBar::translateRequested);
    QSignalSpy dismissedSpy(&bar, &ActionBar::dismissed);

    bar.offer(QPoint(100, 100));
    if (!bar.isVisible()) {
        qWarning("FAIL: bar not visible after offer()");
        return 1;
    }

    auto *button = bar.findChild<QToolButton *>();
    if (!button) {
        qWarning("FAIL: no button found");
        return 1;
    }
    QTest::mouseClick(button, Qt::LeftButton);

    if (translateSpy.count() != 1) {
        qWarning("FAIL: translateRequested count = %d", translateSpy.count());
        return 1;
    }
    if (bar.isVisible()) {
        qWarning("FAIL: bar still visible after click");
        return 1;
    }
    if (dismissedSpy.count() != 1) {
        qWarning("FAIL: dismissed count = %d", dismissedSpy.count());
        return 1;
    }

    QTextStream(stdout) << "PASS" << '\n';
    return 0;
}
