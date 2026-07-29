#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>

using namespace Qt::StringLiterals;

AppSettings AppSettings::load()
{
    const QSettings store(u"translator"_s, u"translator"_s);
    AppSettings s;
    s.apiKey = store.value(u"apiKey"_s).toString();
    s.baseUrl = store.value(u"baseUrl"_s, s.baseUrl).toString();
    s.model = store.value(u"model"_s, s.model).toString();
    s.targetLanguage = store.value(u"targetLanguage"_s, s.targetLanguage).toString();
    s.monitorEnabled = store.value(u"monitorEnabled"_s, true).toBool();
    return s;
}

void AppSettings::save() const
{
    QSettings store(u"translator"_s, u"translator"_s);
    store.setValue(u"apiKey"_s, apiKey);
    store.setValue(u"baseUrl"_s, baseUrl);
    store.setValue(u"model"_s, model);
    store.setValue(u"targetLanguage"_s, targetLanguage);
    store.setValue(u"monitorEnabled"_s, monitorEnabled);
}

QString AppSettings::effectiveApiKey() const
{
    const QString envKey = QProcessEnvironment::systemEnvironment().value(u"DEEPSEEK_API_KEY"_s);
    return envKey.isEmpty() ? apiKey : envKey;
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_apiKeyEdit(new QLineEdit(this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_modelCombo(new QComboBox(this))
    , m_languageCombo(new QComboBox(this))
    , m_enabledCheck(new QCheckBox(tr("Translate automatically when text is selected"), this))
{
    setWindowTitle(tr("Translator Settings"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("emblem-system")));
    setMinimumWidth(420);

    // System-native styling: palette-driven inputs, accent only on the
    // suggested action, flat Cancel — matches the popup/panel language.
    setStyleSheet(R"(
        QLineEdit, QComboBox {
            padding: 6px 8px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
        }
        QLineEdit:focus, QComboBox:focus { border: 1px solid palette(link); }
        QComboBox::drop-down { border: none; width: 24px; }
        QPushButton {
            padding: 6px 18px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(button);
        }
        QPushButton:hover { background: palette(midlight); }
        QPushButton:default {
            background: palette(link);
            color: white;
            border: none;
        }
        QPushButton:default:hover { background: palette(highlight); }
        QCheckBox { spacing: 6px; }
    )"_L1);

    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("sk-… (or set DEEPSEEK_API_KEY)"));
    m_baseUrlEdit->setPlaceholderText(u"https://api.deepseek.com"_s);

    m_modelCombo->addItem(u"deepseek-v4-flash"_s);
    m_modelCombo->addItem(u"deepseek-v4-pro"_s);
    m_modelCombo->setEditable(true);

    m_languageCombo->addItem(tr("Auto (Chinese ↔ English)"), u"auto"_s);
    m_languageCombo->addItem(tr("Simplified Chinese"), u"zh"_s);
    m_languageCombo->addItem(tr("English"), u"en"_s);

    auto *envNote = new QLabel(tr("The API key is stored locally and only sent to the base URL above.\n"
                                  "DEEPSEEK_API_KEY overrides the stored key."), this);
    envNote->setWordWrap(true);
    envNote->setStyleSheet("color: palette(mid); font-size: 11px;"_L1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout(this);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);
    form->addRow(tr("API key:"), m_apiKeyEdit);
    form->addRow(tr("Base URL:"), m_baseUrlEdit);
    form->addRow(tr("Model:"), m_modelCombo);
    form->addRow(tr("Translate to:"), m_languageCombo);
    form->addRow(QString(), m_enabledCheck);
    form->addRow(envNote);
    form->addRow(buttons);
}

void SettingsDialog::setSettings(const AppSettings &settings)
{
    m_apiKeyEdit->setText(settings.apiKey);
    m_baseUrlEdit->setText(settings.baseUrl);
    m_modelCombo->setCurrentText(settings.model);
    int langIndex = m_languageCombo->findData(settings.targetLanguage);
    if (langIndex < 0)
        langIndex = m_languageCombo->findData(u"zh"_s);
    m_languageCombo->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    m_enabledCheck->setChecked(settings.monitorEnabled);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s;
    s.apiKey = m_apiKeyEdit->text().trimmed();
    s.baseUrl = m_baseUrlEdit->text().trimmed();
    if (s.baseUrl.isEmpty())
        s.baseUrl = u"https://api.deepseek.com"_s;
    while (s.baseUrl.endsWith(u'/'))
        s.baseUrl.chop(1);
    s.model = m_modelCombo->currentText().trimmed();
    if (s.model.isEmpty())
        s.model = u"deepseek-v4-flash"_s;
    s.targetLanguage = m_languageCombo->currentData().toString();
    s.monitorEnabled = m_enabledCheck->isChecked();
    return s;
}
