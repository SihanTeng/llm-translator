#include "AppController.h"

#include "ActionBar.h"
#include "DeepSeekClient.h"
#include "PopupWindow.h"
#include "SelectionMonitor.h"
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
    , m_client(new DeepSeekClient(this))
    , m_popup(new PopupWindow)
    , m_actionBar(new ActionBar) {
    applySettings(AppSettings::load());

    connect(m_monitor, &SelectionMonitor::selectionReady, this, &AppController::onSelection);
    connect(m_monitor, &SelectionMonitor::selectionCleared, m_actionBar, &QWidget::hide);
    connect(
        m_actionBar, &ActionBar::translateRequested, this, &AppController::startPendingTranslation);
    connect(m_popup, &PopupWindow::settingsRequested, this, &AppController::openSettings);
    // Allow re-selecting the same text after the bar was dismissed.
    connect(m_actionBar, &ActionBar::dismissed, m_monitor, &SelectionMonitor::resetLastEmitted);

    // Short selections go through JSON mode (the model decides word vs
    // phrase); the raw JSON is buffered and rendered at the end.
    connect(m_client, &DeepSeekClient::tokenReceived, this, [this](const QString &delta) {
        if (m_jsonMode) {
            m_jsonBuffer += delta;
            return;
        }
        m_popup->appendToken(delta);
        emit TranslationToken(delta);
    });
    connect(m_client, &DeepSeekClient::requestFinished, this, [this] {
        if (m_jsonMode) {
            const QJsonObject obj = QJsonDocument::fromJson(m_jsonBuffer.toUtf8()).object();
            if (obj["type"_L1].toString() == "phrase"_L1
                && !obj["translation"_L1].toString().isEmpty()) {
                // The model classified the selection as a phrase: show the
                // translation as plain text.
                const QString translation = obj["translation"_L1].toString();
                m_popup->setResult(translation.toHtmlEscaped());
                emit TranslationToken(translation);
            } else {
                const QString html = formatWordCardHtml(m_jsonBuffer);
                m_popup->setResult(html);
                // The Shell extension renders its own structured card from
                // the raw JSON (St widgets cannot render HTML).
                emit TranslationWordCard(m_jsonBuffer);
                // Plain-text form for older extension versions.
                QTextDocument doc;
                doc.setHtml(html);
                emit TranslationToken(doc.toPlainText().trimmed());
            }
        }
        emit TranslationFinished();
    });
    connect(m_client, &DeepSeekClient::errorOccurred, this, [this](const QString &message) {
        m_popup->showError(message);
        emit TranslationError(message);
    });

    // D-Bus entry point for the GNOME Shell extension (Wayland sessions).
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.translator.App"));
    bus.registerObject(QStringLiteral("/org/translator/App"), this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);

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
    if (!m_settings.monitorEnabled)
        return;
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

void AppController::applySettings(const AppSettings &settings) {
    m_settings = settings;
    m_settings.save();
    m_client->setApiKey(m_settings.effectiveApiKey());
    m_client->setBaseUrl(m_settings.baseUrl);
    m_client->setModel(m_settings.model);
    m_monitor->setEnabled(m_settings.monitorEnabled);
}

QString AppController::buildSystemPrompt(bool jsonMode) const {
    if (jsonMode) {
        QString target;
        if (m_settings.targetLanguage == "zh"_L1)
            target = tr("translate it into Simplified Chinese");
        else if (m_settings.targetLanguage == "en"_L1)
            target = tr("translate it into English");
        else
            target = tr("if the text is Chinese, translate it into English; otherwise translate "
                        "it into Simplified Chinese");

        return tr("You are a translation and dictionary assistant. The user message contains a "
                  "short text after \"Text:\" and may also contain the sentence it was found in "
                  "after \"Sentence:\". First DECIDE whether the text is a single word or term "
                  "(including compounds, hyphenated words, phrasal verbs, idioms and set phrases "
                  "like \"ice cream\", \"take off\", \"画蛇添足\") or a longer phrase/sentence, "
                  "then respond ONLY with a JSON object (no markdown, no extra text) in one of "
                  "these two forms:\n"
                  "1) Word/term: {\"type\": \"word\", \"word\": \"...\", \"phonetic\": \"IPA for "
                  "English, pinyin for Chinese, may be empty\", \"pos\": \"part of speech, e.g. "
                  "\\\"n.\\\"\", \"meaning\": \"a concise definition in the SAME language as the "
                  "text, using simpler terms\", \"explanation\": \"1-2 sentences in the SAME "
                  "language as the text, using simple everyday words\", \"example\": \"one short "
                  "example sentence in the SAME language as the text\"}. If a Sentence is given, "
                  "explain the meaning the word has IN THAT SENTENCE, not the generic "
                  "dictionary one.\n"
                  "2) Phrase/sentence: {\"type\": \"phrase\", \"translation\": \"...\"} — %1. If "
                  "a Sentence is given, use it to resolve ambiguity (pronouns, tense, "
                  "context-specific senses).")
            .arg(target);
    }
    if (m_settings.targetLanguage == "zh"_L1)
        return tr(
            "You are a translation engine. Translate the user's text into Simplified Chinese. "
            "Output only the translation: no explanations, no quotes, no markup.");
    if (m_settings.targetLanguage == "en"_L1)
        return tr("You are a translation engine. Translate the user's text into English. "
                  "Output only the translation: no explanations, no quotes, no markup.");
    return tr("You are a translation engine. If the user's text is Chinese, translate it into "
              "English; otherwise translate it into Simplified Chinese. "
              "Output only the translation: no explanations, no quotes, no markup.");
}
