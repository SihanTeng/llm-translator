#include "AppController.h"

#include "ActionBar.h"
#include "HistoryDialog.h"
#include "HistoryStore.h"
#include "Languages.h"
#include "LlmClient.h"
#include "PopupWindow.h"
#include "Prompts.h"
#include "Provider.h"
#include "SelectionMonitor.h"
#include "Speaker.h"
#include "Updater.h"
#include "WordFormatter.h"

#include <QApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QTextDocument>
#include <QTimer>

using namespace Qt::StringLiterals;

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_monitor(new SelectionMonitor(this))
    , m_popup(new PopupWindow)
    , m_actionBar(new ActionBar)
    , m_speaker(new Speaker(this))
    , m_history(new HistoryStore(this)) {
    applySettings(AppSettings::load()); // creates and configures the LLM client

    connect(m_monitor, &SelectionMonitor::selectionReady, this, &AppController::onSelection);
    connect(m_monitor, &SelectionMonitor::selectionCleared, m_actionBar, &QWidget::hide);
    connect(
        m_actionBar, &ActionBar::translateRequested, this, &AppController::startPendingTranslation);
    connect(m_popup, &PopupWindow::settingsRequested, this, &AppController::openSettings);
    connect(m_popup, &PopupWindow::speakRequested, m_speaker, &Speaker::speak);
    // Allow re-selecting the same text after the bar was dismissed.
    connect(m_actionBar, &ActionBar::dismissed, m_monitor, &SelectionMonitor::resetLastEmitted);

    // D-Bus entry point for the GNOME Shell extension (Wayland sessions).
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.translator.App"));
    bus.registerObject(QStringLiteral("/org/translator/App"), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *menu = new QMenu;
        menu->addAction(tr("History…"), this, &AppController::openHistory);
        menu->addAction(tr("Settings…"), this, &AppController::openSettings);
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

    // OTA: one quiet check shortly after startup (opt-out in Settings).
    m_updater = new Updater(this);
    connect(m_updater, &Updater::updateAvailable, this,
        [this](const QString &version, const QString &url) {
            if (!Updater::isAppImage()) {
                QMessageBox::information(nullptr, tr("Update available"),
                    tr("Version %1 is available (you have %2).\n"
                       "Update via your package manager or the releases page.")
                        .arg(version, QStringLiteral(TRANSLATOR_VERSION)));
                QDesktopServices::openUrl(
                    QUrl(QStringLiteral("https://github.com/SihanTeng/llm-translator/releases")));
                return;
            }
            const auto answer = QMessageBox::question(nullptr, tr("Update available"),
                tr("Version %1 is available (you have %2). Download and install now?")
                    .arg(version, QStringLiteral(TRANSLATOR_VERSION)));
            if (answer != QMessageBox::Yes)
                return;
            m_updateProgress = new QProgressDialog(tr("Downloading update…"), tr("Cancel"), 0, 100);
            m_updateProgress->setWindowModality(Qt::ApplicationModal);
            m_updateProgress->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_updater, &Updater::downloadProgress, m_updateProgress,
                [this](qint64 received, qint64 total) {
                    if (total > 0) {
                        m_updateProgress->setMaximum(100);
                        m_updateProgress->setValue(static_cast<int>(received * 100 / total));
                    }
                });
            connect(
                m_updateProgress, &QProgressDialog::canceled, m_updater, &Updater::cancelDownload);
            m_updater->downloadAndInstall(url);
        });
    connect(m_updater, &Updater::installed, this, [this](const QString &path) {
        if (m_updateProgress) {
            m_updateProgress->close();
            m_updateProgress = nullptr;
        }
        QMessageBox::information(nullptr, tr("Update installed"),
            tr("Updated to the latest version. It takes effect the next time "
               "you start Translator (%1).")
                .arg(path));
    });
    connect(m_updater, &Updater::failed, this, [this](const QString &message) {
        if (m_updateProgress) {
            m_updateProgress->close();
            m_updateProgress = nullptr;
        }
        qWarning() << "updater:" << message;
    });
    if (m_settings.autoUpdate)
        QTimer::singleShot(10000, this, &AppController::checkForUpdates);
}

void AppController::checkForUpdates() {
    m_updater->check();
}

void AppController::TranslateSelection(const QString &text, int x, int y) {
    TranslateSelectionWithContext(text, QString(), x, y);
}

void AppController::TranslateSelectionWithContext(
    const QString &text, const QString &context, int x, int y) {
    if (m_shellUiEnabled) {
        // The extension renders the bar and the translation panel itself;
        // just run the request. The result reaches it via TranslationToken.
        startTranslation(text, context, QPoint(x, y), false);
        return;
    }
    offerTranslation(text, context, QPoint(x, y));
}

void AppController::SetShellUiEnabled(bool enabled) {
    m_shellUiEnabled = enabled;
}

void AppController::SpeakText(const QString &text) {
    m_speaker->speak(text);
}

void AppController::ShowSettings() {
    // openSettings() runs a modal dialog; defer it so the D-Bus method
    // returns immediately instead of blocking the caller until it closes.
    QTimer::singleShot(0, this, &AppController::openSettings);
}

void AppController::onSelection(const QString &text) {
    offerTranslation(text, QString(), QCursor::pos());
}

void AppController::offerTranslation(
    const QString &text, const QString &context, const QPoint &globalPos) {
    m_client->cancel();
    m_popup->hide();
    m_pendingText = text;
    m_pendingContext = context;
    m_pendingPos = globalPos;
    m_actionBar->offer(globalPos);
}

void AppController::startPendingTranslation() {
    if (m_pendingText.isEmpty())
        return;
    startTranslation(m_pendingText, m_pendingContext, m_pendingPos, true);
}

void AppController::startTranslation(
    const QString &text, const QString &context, const QPoint &globalPos, bool showPopup) {
    m_client->cancel();
    m_jsonMode = isShortText(text);
    m_jsonBuffer.clear();
    m_currentSource = text;
    m_resultBuffer.clear();
    if (showPopup)
        m_popup->startTranslation(text, globalPos);

    // In JSON mode the model classifies the selection itself; a captured
    // sentence lets it explain/translate in context.
    QString userContent = text;
    if (m_jsonMode) {
        userContent = "Text: "_L1 + text;
        if (!context.isEmpty())
            userContent += "\nSentence: "_L1 + context;
    }

    m_client->translate(userContent, buildSystemPrompt(m_jsonMode), m_jsonMode);
}

// Short selections are handled in JSON mode, where the model decides whether
// the text is a word/term (dictionary card) or a phrase (translation).
bool AppController::isShortText(const QString &text) {
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 120)
        return false;
    return trimmed.split(whitespace, Qt::SkipEmptyParts).size() <= 8;
}

void AppController::openSettings() {
    SettingsDialog dialog;
    dialog.setSettings(m_settings);
    if (dialog.exec() == QDialog::Accepted)
        applySettings(dialog.settings());
}

void AppController::openHistory() {
    HistoryDialog dialog(m_history);
    dialog.exec();
}

void AppController::applySettings(const AppSettings &settings) {
    const bool providerChanged = settings.provider != m_settings.provider;
    m_settings = settings;
    m_settings.save();
    if (!m_client || providerChanged)
        rebuildClient();
    else
        configureClient();
    emit ExcludedAppsChanged(m_settings.excludedApps);
}

void AppController::configureClient() {
    const ProviderInfo *info = providerById(m_settings.provider);
    if (!info)
        info = providerById(QStringLiteral("deepseek"));
    const ProviderSettings entry = m_settings.currentProviderSettings();
    // entry.baseUrl is empty unless overridden (Settings exposes it only
    // for the "custom" provider); configure() falls back to the default.
    m_client->configure(*info, m_settings.effectiveApiKey(), entry.model, entry.baseUrl);
}

void AppController::rebuildClient() {
    const ProviderInfo *info = providerById(m_settings.provider);
    if (!info) // corrupt/corner-case id: fall back to DeepSeek-compatible
        info = providerById(QStringLiteral("deepseek"));
    if (m_client)
        m_client->deleteLater();
    m_client = LlmClient::create(*info, this);

    // Short selections go through JSON mode (the model decides word vs
    // phrase); the raw JSON is buffered and rendered at the end.
    connect(m_client, &LlmClient::tokenReceived, this, [this](const QString &delta) {
        if (m_jsonMode) {
            m_jsonBuffer += delta;
            return;
        }
        m_resultBuffer += delta;
        m_popup->appendToken(delta);
        emit TranslationToken(delta);
    });
    connect(m_client, &LlmClient::requestFinished, this, [this] {
        if (m_jsonMode) {
            // Providers without response_format may wrap the JSON in prose
            // or code fences — extract it leniently.
            const QString json = extractJsonPayload(m_jsonBuffer);
            const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
            if (obj["type"_L1].toString() == "phrase"_L1
                && !obj["translation"_L1].toString().isEmpty()) {
                // The model classified the selection as a phrase: show the
                // translation as plain text.
                const QString translation = obj["translation"_L1].toString();
                m_popup->setResult(translation.toHtmlEscaped());
                emit TranslationToken(translation);
                m_history->add(m_currentSource, translation);
            } else {
                const QString html = formatWordCardHtml(json);
                m_popup->setResult(html);
                // The Shell extension renders its own structured card from
                // the raw JSON (St widgets cannot render HTML).
                emit TranslationWordCard(json);
                // Plain-text form for older extension versions.
                QTextDocument doc;
                doc.setHtml(html);
                const QString plain = doc.toPlainText().trimmed();
                emit TranslationToken(plain);
                m_history->add(m_currentSource, plain);
            }
        } else {
            m_history->add(m_currentSource, m_resultBuffer);
        }
        emit TranslationFinished();
    });
    connect(m_client, &LlmClient::errorOccurred, this, [this](const QString &message) {
        m_popup->showError(message);
        emit TranslationError(message);
    });

    configureClient();
}

QString AppController::buildSystemPrompt(bool jsonMode) const {
    // Unknown/corrupt codes fall back to Simplified Chinese. Templates come
    // from spec/prompts.json (shared with future clients).
    QString language = languageEnglishName(m_settings.targetLanguage);
    if (language.isEmpty())
        language = QStringLiteral("Simplified Chinese");
    return jsonMode ? Prompts::word(language) : Prompts::phrase(language);
}
