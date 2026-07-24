#include "AppController.h"

#include "ActionBar.h"
#include "DeepSeekClient.h"
#include "PopupWindow.h"
#include "SelectionMonitor.h"

#include <QApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPalette>
#include <QPixmap>
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
    , m_actionBar(new ActionBar)
{
    applySettings(AppSettings::load());

    connect(m_monitor, &SelectionMonitor::selectionReady,
            this, &AppController::onSelection);
    connect(m_monitor, &SelectionMonitor::selectionCleared,
            m_actionBar, &QWidget::hide);
    connect(m_actionBar, &ActionBar::translateRequested,
            this, &AppController::startPendingTranslation);
    connect(m_popup, &PopupWindow::settingsRequested,
            this, &AppController::openSettings);
    // Allow re-selecting the same text after the bar was dismissed.
    connect(m_actionBar, &ActionBar::dismissed,
            m_monitor, &SelectionMonitor::resetLastEmitted);

    // Word mode (single word -> structured dictionary explanation) is not
    // streamed to the UI; the raw JSON is buffered and formatted at the end.
    connect(m_client, &DeepSeekClient::tokenReceived, this, [this](const QString &delta) {
        if (m_wordMode) {
            m_wordBuffer += delta;
            return;
        }
        m_popup->appendToken(delta);
        emit TranslationToken(delta);
    });
    connect(m_client, &DeepSeekClient::requestFinished, this, [this] {
        if (m_wordMode) {
            const QString html = formatWordResult(m_wordBuffer);
            m_popup->setResult(html);
            // The Shell extension renders its own structured card from the
            // raw JSON (St widgets cannot render HTML).
            emit TranslationWordCard(m_wordBuffer);
            // Plain-text form for older extension versions.
            QTextDocument doc;
            doc.setHtml(html);
            emit TranslationToken(doc.toPlainText().trimmed());
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
}

void AppController::TranslateSelection(const QString &text, int x, int y)
{
    TranslateSelectionWithContext(text, QString(), x, y);
}

void AppController::TranslateSelectionWithContext(const QString &text, const QString &context,
                                                  int x, int y)
{
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

void AppController::SetShellUiEnabled(bool enabled)
{
    m_shellUiEnabled = enabled;
}

void AppController::ShowSettings()
{
    // openSettings() runs a modal dialog; defer it so the D-Bus method
    // returns immediately instead of blocking the caller until it closes.
    QTimer::singleShot(0, this, &AppController::openSettings);
}

void AppController::onSelection(const QString &text)
{
    offerTranslation(text, QString(), QCursor::pos());
}

void AppController::offerTranslation(const QString &text, const QString &context,
                                     const QPoint &globalPos)
{
    m_client->cancel();
    m_popup->hide();
    m_pendingText = text;
    m_pendingContext = context;
    m_pendingPos = globalPos;
    m_actionBar->offer(globalPos);
}

void AppController::startPendingTranslation()
{
    if (m_pendingText.isEmpty())
        return;
    startTranslation(m_pendingText, m_pendingContext, m_pendingPos, true);
}

void AppController::startTranslation(const QString &text, const QString &context,
                                     const QPoint &globalPos, bool showPopup)
{
    m_client->cancel();
    m_wordMode = isSingleWord(text);
    m_wordBuffer.clear();
    if (showPopup)
        m_popup->startTranslation(text, globalPos);

    // In word mode, a captured sentence lets the model explain the word as
    // used in that sentence.
    QString userContent = text;
    if (m_wordMode && !context.isEmpty())
        userContent = "Word: "_L1 + text + "\nSentence: "_L1 + context;

    m_client->translate(userContent, buildSystemPrompt(m_wordMode), m_wordMode);
}

bool AppController::isSingleWord(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s"));
    const QString trimmed = text.trimmed();
    return !trimmed.isEmpty() && trimmed.size() < 40 && !trimmed.contains(whitespace);
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

QString AppController::buildSystemPrompt(bool wordMode) const
{
    if (wordMode) {
        // Monolingual learner's dictionary: explain the word in its own
        // language using simpler terms. When the user message includes a
        // "Sentence:" line, explain the word AS USED IN THAT SENTENCE.
        // (Sentences, by contrast, are translated into the user's mother
        // tongue per the target setting.)
        return tr("You are a monolingual learner's dictionary. For the single word the user "
                  "provides, respond ONLY with a JSON object (no markdown, no extra text) with "
                  "these string fields: \"word\", \"phonetic\" (IPA for English, pinyin for "
                  "Chinese, may be empty), \"pos\" (part of speech, e.g. \"n.\"), \"meaning\" "
                  "(a concise definition in the SAME language as the word, using simpler "
                  "terms), \"explanation\" (1-2 sentences in the SAME language as the word, "
                  "using simple everyday words, about usage), \"example\" (one short example "
                  "sentence in the SAME language as the word). If the user message includes "
                  "a \"Sentence:\" line, the word appears in that sentence: give the meaning "
                  "the word has IN THAT SENTENCE and base the explanation on that usage.");
    }
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

QString AppController::formatWordResult(const QString &jsonPayload) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonPayload.toUtf8());
    if (!doc.isObject())
        return jsonPayload.toHtmlEscaped(); // model did not produce valid JSON

    // Palette-driven, so the card follows the system theme (Adwaita
    // light/dark) instead of hardcoded colors.
    const QPalette palette = qApp->palette();
    const QString accent = palette.color(QPalette::Link).name();
    const QString muted = palette.color(QPalette::PlaceholderText).name();

    const QJsonObject obj = doc.object();
    const auto field = [&obj](const char *key) {
        return obj[QLatin1StringView(key)].toString().toHtmlEscaped();
    };

    QString html = "<div style='margin-bottom:6px'>"
                   "<span style='font-size:18px; font-weight:bold; color:" + accent + "'>"
                   + field("word") + "</span>";
    const QString phonetic = field("phonetic");
    if (!phonetic.isEmpty())
        html += "  <span style='color:" + muted + "'>" + phonetic + "</span>";
    const QString pos = field("pos");
    if (!pos.isEmpty())
        html += "  <span style='color:" + muted + "'>" + pos + "</span>";
    html += "</div>";

    const QString meaning = field("meaning");
    if (!meaning.isEmpty())
        html += "<div style='font-weight:600; margin-top:8px; margin-bottom:8px'>" + meaning + "</div>";
    const QString explanation = field("explanation");
    if (!explanation.isEmpty())
        html += "<div style='margin-bottom:8px'>" + explanation + "</div>";
    const QString example = field("example");
    if (!example.isEmpty())
        html += "<div style='color:" + muted + "'>" + example + "</div>";
    return html;
}
