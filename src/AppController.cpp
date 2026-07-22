#include "AppController.h"

#include "ActionBar.h"
#include "DeepSeekClient.h"
#include "PopupWindow.h"
#include "SelectionMonitor.h"

#include <QApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QMenu>
#include <QPixmap>
#include <QSystemTrayIcon>

using namespace Qt::StringLiterals;

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_monitor(new SelectionMonitor(this))
    , m_client(new DeepSeekClient(this))
    , m_popup(new PopupWindow)
    , m_actionBar(new ActionBar)
{
    applySettings(AppSettings::load());

    connect(m_monitor, &SelectionMonitor::selectionReady,
            this, &AppController::onSelection);
    connect(m_actionBar, &ActionBar::translateRequested,
            this, &AppController::startPendingTranslation);
    connect(m_actionBar, &ActionBar::settingsRequested,
            this, &AppController::openSettings);
    // Allow re-selecting the same text after the bar was dismissed.
    connect(m_actionBar, &ActionBar::dismissed,
            m_monitor, &SelectionMonitor::resetLastEmitted);
    connect(m_client, &DeepSeekClient::tokenReceived,
            m_popup, &PopupWindow::appendToken);
    connect(m_client, &DeepSeekClient::errorOccurred,
            m_popup, &PopupWindow::showError);

    // D-Bus entry point for the GNOME Shell extension (Wayland sessions).
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.translator.App"));
    bus.registerObject(QStringLiteral("/org/translator/App"), this,
                       QDBusConnection::ExportAllSlots);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *menu = new QMenu;
        menu->addAction(tr("Settings…"), this, &AppController::openSettings);
        auto *toggle = menu->addAction(tr("Pause monitoring"), this, [this](bool checked) {
            m_settings.monitorEnabled = !checked;
            m_monitor->setEnabled(m_settings.monitorEnabled);
            m_settings.save();
        });
        toggle->setCheckable(true);
        toggle->setChecked(!m_settings.monitorEnabled);
        menu->addSeparator();
        menu->addAction(tr("Quit"), qApp, &QApplication::quit);

        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::darkCyan);
        m_tray = new QSystemTrayIcon(QIcon(pixmap), this);
        m_tray->setToolTip(tr("Translator"));
        m_tray->setContextMenu(menu);
        m_tray->show();
    }

    // First run: no key anywhere -> open settings so the user can paste one.
    if (m_settings.effectiveApiKey().isEmpty())
        openSettings();
}

void AppController::TranslateSelection(const QString &text, int x, int y)
{
    if (!m_settings.monitorEnabled)
        return;
    offerTranslation(text, QPoint(x, y));
}

void AppController::onSelection(const QString &text)
{
    offerTranslation(text, QCursor::pos());
}

void AppController::offerTranslation(const QString &text, const QPoint &globalPos)
{
    m_client->cancel();
    m_popup->hide();
    m_pendingText = text;
    m_pendingPos = globalPos;
    m_actionBar->offer(globalPos);
}

void AppController::startPendingTranslation()
{
    if (m_pendingText.isEmpty())
        return;
    m_client->cancel();
    m_popup->startTranslation(m_pendingText, m_pendingPos);
    m_client->translate(m_pendingText, buildSystemPrompt());
}

void AppController::openSettings()
{
    SettingsDialog dialog;
    dialog.setSettings(m_settings);
    if (dialog.exec() == QDialog::Accepted)
        applySettings(dialog.settings());
}

void AppController::applySettings(const AppSettings &settings)
{
    m_settings = settings;
    m_settings.save();
    m_client->setApiKey(m_settings.effectiveApiKey());
    m_client->setBaseUrl(m_settings.baseUrl);
    m_client->setModel(m_settings.model);
    m_monitor->setEnabled(m_settings.monitorEnabled);
}

QString AppController::buildSystemPrompt() const
{
    if (m_settings.targetLanguage == "zh"_L1)
        return tr("You are a translation engine. Translate the user's text into Simplified Chinese. "
                  "Output only the translation: no explanations, no quotes, no markup.");
    if (m_settings.targetLanguage == "en"_L1)
        return tr("You are a translation engine. Translate the user's text into English. "
                  "Output only the translation: no explanations, no quotes, no markup.");
    return tr("You are a translation engine. If the user's text is Chinese, translate it into "
              "English; otherwise translate it into Simplified Chinese. "
              "Output only the translation: no explanations, no quotes, no markup.");
}
