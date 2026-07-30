#include "SettingsDialog.h"

#include "AppPickerDialog.h"
#include "Languages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace {
// Parses the "Exclude apps" field: comma/semicolon-separated, trimmed,
// deduplicated case-insensitively.
QStringList parseExcludedApps(const QString &text) {
    QStringList result;
    const QStringList parts = text.split(QRegularExpression(u"[;,]"_s), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString app = part.trimmed();
        if (!app.isEmpty() && !result.contains(app, Qt::CaseInsensitive))
            result.append(app);
    }
    return result;
}
} // namespace

AppSettings AppSettings::load() {
    const QSettings store(u"translator"_s, u"translator"_s);
    AppSettings s;
    s.apiKey = store.value(u"apiKey"_s).toString();
    s.baseUrl = store.value(u"baseUrl"_s, s.baseUrl).toString();
    s.model = store.value(u"model"_s, s.model).toString();
    s.targetLanguage = store.value(u"targetLanguage"_s, s.targetLanguage).toString();
    // Legacy values: "zh" predates the multi-language list, "auto" was removed.
    if (s.targetLanguage == "zh"_L1 || s.targetLanguage == "auto"_L1)
        s.targetLanguage = "zh-CN"_L1;
    s.excludedApps = store.value(u"excludedApps"_s).toStringList();
    s.autoUpdate = store.value(u"autoUpdate"_s, true).toBool();
    return s;
}

void AppSettings::save() const {
    QSettings store(u"translator"_s, u"translator"_s);
    store.setValue(u"apiKey"_s, apiKey);
    store.setValue(u"baseUrl"_s, baseUrl);
    store.setValue(u"model"_s, model);
    store.setValue(u"targetLanguage"_s, targetLanguage);
    store.setValue(u"excludedApps"_s, excludedApps);
    store.setValue(u"autoUpdate"_s, autoUpdate);
}

QString AppSettings::effectiveApiKey() const {
    const QString envKey = QProcessEnvironment::systemEnvironment().value(u"DEEPSEEK_API_KEY"_s);
    return envKey.isEmpty() ? apiKey : envKey;
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_apiKeyEdit(new QLineEdit(this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_modelCombo(new QComboBox(this))
    , m_languageCombo(new QComboBox(this))
    , m_excludedAppsEdit(new QLineEdit(this))
    , m_autoUpdateCheck(new QCheckBox(tr("Automatically check for updates"), this)) {
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

    for (const TargetLanguage &lang : targetLanguages()) {
        const QString english = QString::fromUtf8(lang.englishName);
        const QString nativeName = QString::fromUtf8(lang.nativeName);
        const QString label
            = nativeName == english ? english : u"%1 (%2)"_s.arg(english, nativeName);
        m_languageCombo->addItem(QIcon(u":/flags/%1.png"_s.arg(QLatin1StringView(lang.flag))),
            label, QLatin1StringView(lang.code));
    }

    m_excludedAppsEdit->setPlaceholderText(tr("keepassxc, org.gnome.Terminal"));
    m_excludedAppsEdit->setToolTip(
        tr("Comma-separated window class (WM_CLASS) names. Text selected in these apps "
           "never shows the Translate bar — useful for password managers.\n"
           "GNOME Wayland only. Use Choose… to pick from your installed apps."));

    auto *chooseAppsButton = new QPushButton(tr("Choose…"), this);
    connect(chooseAppsButton, &QPushButton::clicked, this, [this] {
        AppPickerDialog picker(this);
        picker.setCheckedWmClasses(parseExcludedApps(m_excludedAppsEdit->text()));
        if (picker.exec() != QDialog::Accepted)
            return;
        QStringList classes = parseExcludedApps(m_excludedAppsEdit->text());
        const QStringList picked = picker.selectedWmClasses();
        for (const QString &cls : picked) {
            if (!classes.contains(cls, Qt::CaseInsensitive))
                classes.append(cls);
        }
        m_excludedAppsEdit->setText(classes.join(u", "_s));
    });

    auto *envNote
        = new QLabel(tr("The API key is stored locally and only sent to the base URL above.\n"
                        "DEEPSEEK_API_KEY overrides the stored key."),
            this);
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
    auto *excludedRow = new QWidget(this);
    auto *excludedLayout = new QHBoxLayout(excludedRow);
    excludedLayout->setContentsMargins(0, 0, 0, 0);
    excludedLayout->addWidget(m_excludedAppsEdit, 1);
    excludedLayout->addWidget(chooseAppsButton);
    form->addRow(tr("Exclude apps:"), excludedRow);
    form->addRow(QString(), m_autoUpdateCheck);
    form->addRow(envNote);
    form->addRow(buttons);
}

void SettingsDialog::setSettings(const AppSettings &settings) {
    m_apiKeyEdit->setText(settings.apiKey);
    m_baseUrlEdit->setText(settings.baseUrl);
    m_modelCombo->setCurrentText(settings.model);
    int langIndex = m_languageCombo->findData(settings.targetLanguage);
    if (langIndex < 0)
        langIndex = m_languageCombo->findData(u"zh-CN"_s);
    m_languageCombo->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    m_excludedAppsEdit->setText(settings.excludedApps.join(u", "_s));
    m_autoUpdateCheck->setChecked(settings.autoUpdate);
}

AppSettings SettingsDialog::settings() const {
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
    s.excludedApps = parseExcludedApps(m_excludedAppsEdit->text());
    s.autoUpdate = m_autoUpdateCheck->isChecked();
    return s;
}
