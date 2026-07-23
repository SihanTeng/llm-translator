#include "SelectionMonitor.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>

using namespace Qt::StringLiterals;

SelectionMonitor::SelectionMonitor(QObject *parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);

    // Reliable PRIMARY-selection monitoring only exists on X11. On Wayland
    // the compositor only delivers selections to a focused window, so there
    // selections arrive via the GNOME Shell extension over D-Bus instead.
    if (QGuiApplication::platformName() != "xcb"_L1) {
        m_enabled = false;
        return;
    }

    connect(m_clipboard, &QClipboard::selectionChanged,
            this, &SelectionMonitor::onSelectionChanged);

    connect(m_debounce, &QTimer::timeout, this, [this] {
        const QString text = m_pending.trimmed();
        m_pending.clear();
        if (text.isEmpty()) {
            emit selectionCleared();
            return;
        }
        if (text == m_lastEmitted)
            return;
        if (text.size() < kMinLength || text.size() > kMaxLength)
            return;
        m_lastEmitted = text;
        emit selectionReady(text);
    });
}

void SelectionMonitor::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
        m_debounce->stop();
}

void SelectionMonitor::onSelectionChanged()
{
    if (!m_enabled)
        return;
    // Ignore selections made inside our own popup window.
    if (m_clipboard->ownsSelection())
        return;
    m_pending = m_clipboard->text(QClipboard::Selection);
    m_debounce->start();
}
