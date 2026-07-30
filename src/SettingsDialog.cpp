#include "SettingsDialog.h"

#include "AppPickerDialog.h"
#include "Languages.h"
#include "Provider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
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

QString defaultBaseUrl(const QString &providerId) {
    const ProviderInfo *info = providerById(providerId);
    return info ? info->baseUrl : QString();
}
} // namespace

AppSettings AppSettings::load() {
    QSettings store(u"translator"_s, u"translator"_s);
    AppSettings s;
    s.provider = store.value(u"provider"_s).toString();

    if (s.provider.isEmpty()) {
        // Migration from pre-provider configs: top-level apiKey/baseUrl/
        // model become the deepseek entry, or "custom" when the base URL
        // was pointed elsewhere.
        const QString oldKey = store.value(u"apiKey"_s).toString();
        const QString oldBase = store.value(u"baseUrl"_s).toString();
        const QString oldModel = store.value(u"model"_s).toString();
        if (!oldBase.isEmpty() && oldBase != defaultBaseUrl(u"deepseek"_s)) {
            s.provider = u"custom"_s;
            s.perProvider.insert(u"custom"_s,
                { oldKey, oldModel.isEmpty() ? u"deepseek-v4-flash"_s : oldModel, oldBase });
        } else {
            s.provider = u"deepseek"_s;
            if (!oldKey.isEmpty() || !oldModel.isEmpty()) {
                s.perProvider.insert(u"deepseek"_s,
                    { oldKey, oldModel.isEmpty() ? u"deepseek-v4-flash"_s : oldModel, QString() });
            }
        }
    }

    for (const ProviderInfo &info : providers()) {
        const QString id = info.id;
        ProviderSettings &entry = s.perProvider[id]; // ensure every provider has an entry
        const QString group = id + u'/';
        const QString key = store.value(group + u"apiKey"_s).toString();
        if (!key.isEmpty())
            entry.apiKey = key;
        const QString model = store.value(group + u"model"_s).toString();
        if (!model.isEmpty())
            entry.model = model;
        const QString base = store.value(group + u"baseUrl"_s).toString();
        if (!base.isEmpty())
            entry.baseUrl = base;
        if (entry.model.isEmpty())
            entry.model = info.defaultModel;
    }

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
    store.setValue(u"provider"_s, provider);
    for (auto it = perProvider.constBegin(); it != perProvider.constEnd(); ++it) {
        const QString group = it.key() + u'/';
        store.setValue(group + u"apiKey"_s, it.value().apiKey);
        store.setValue(group + u"model"_s, it.value().model);
        store.setValue(group + u"baseUrl"_s, it.value().baseUrl);
    }
    store.setValue(u"targetLanguage"_s, targetLanguage);
    store.setValue(u"excludedApps"_s, excludedApps);
    store.setValue(u"autoUpdate"_s, autoUpdate);
}

ProviderSettings AppSettings::currentProviderSettings() const {
    return perProvider.value(provider);
}

QString AppSettings::effectiveApiKey() const {
    const ProviderInfo *info = providerById(provider);
    if (info && !info->envVar.isEmpty()) {
        const QString envKey = QProcessEnvironment::systemEnvironment().value(info->envVar);
        if (!envKey.isEmpty())
            return envKey;
    }
    return currentProviderSettings().apiKey;
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_providerCombo(new QComboBox(this))
    , m_apiKeyEdit(new QLineEdit(this))
    , m_modelCombo(new QComboBox(this))
    , m_baseUrlLabel(new QLabel(tr("Base URL:"), this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_languageCombo(new QComboBox(this))
    , m_excludedAppsEdit(new QLineEdit(this))
    , m_autoUpdateCheck(new QCheckBox(tr("Automatically check for updates"), this))
    , m_keyLink(new QLabel(this))
    , m_envNote(new QLabel(this)) {
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

    for (const ProviderInfo &info : providers()) {
        m_providerCombo->addItem(info.name, info.id);
    }
    connect(
        m_providerCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::onProviderChanged);

    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_modelCombo->setEditable(true);
    m_keyLink->setTextFormat(Qt::RichText);
    m_keyLink->setOpenExternalLinks(true);
    m_keyLink->setStyleSheet("color: palette(link); font-size: 11px;"_L1);
    m_envNote->setWordWrap(true);
    m_envNote->setStyleSheet("color: palette(mid); font-size: 11px;"_L1);

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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout(this);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);
    form->addRow(tr("Provider:"), m_providerCombo);
    form->addRow(tr("API key:"), m_apiKeyEdit);
    form->addRow(QString(), m_keyLink);
    form->addRow(tr("Model:"), m_modelCombo);
    form->addRow(m_baseUrlLabel, m_baseUrlEdit);
    form->addRow(tr("Translate to:"), m_languageCombo);
    auto *excludedRow = new QWidget(this);
    auto *excludedLayout = new QHBoxLayout(excludedRow);
    excludedLayout->setContentsMargins(0, 0, 0, 0);
    excludedLayout->addWidget(m_excludedAppsEdit, 1);
    excludedLayout->addWidget(chooseAppsButton);
    form->addRow(tr("Exclude apps:"), excludedRow);
    form->addRow(QString(), m_autoUpdateCheck);
    form->addRow(m_envNote);
    form->addRow(buttons);
}

void SettingsDialog::onProviderChanged(int index) {
    stashCurrentFields();
    loadFieldsFor(m_providerCombo->itemData(index).toString());
}

void SettingsDialog::stashCurrentFields() {
    if (m_currentProviderId.isEmpty())
        return;
    ProviderSettings &draft = m_drafts[m_currentProviderId];
    draft.apiKey = m_apiKeyEdit->text().trimmed();
    draft.model = m_modelCombo->currentText().trimmed();
    draft.baseUrl = m_baseUrlEdit->text().trimmed();
}

void SettingsDialog::loadFieldsFor(const QString &providerId) {
    m_currentProviderId = providerId;
    const ProviderInfo *info = providerById(providerId);
    const ProviderSettings draft = m_drafts.value(providerId);

    m_apiKeyEdit->setText(draft.apiKey);
    m_apiKeyEdit->setPlaceholderText(
        info && !info->envVar.isEmpty() ? tr("or set %1").arg(info->envVar) : QString());

    m_modelCombo->clear();
    if (info) {
        m_modelCombo->addItem(info->defaultModel);
        if (!info->altModel.isEmpty())
            m_modelCombo->addItem(info->altModel);
    }
    m_modelCombo->setCurrentText(draft.model.isEmpty() && info ? info->defaultModel : draft.model);

    const bool isCustom = providerId == "custom"_L1;
    m_baseUrlLabel->setVisible(isCustom);
    m_baseUrlEdit->setVisible(isCustom);
    m_baseUrlEdit->setText(draft.baseUrl);
    m_baseUrlEdit->setPlaceholderText(u"https://api.example.com/v1"_s);

    const bool hasKeyPage = info && !info->keyPage.isEmpty();
    m_keyLink->setVisible(hasKeyPage);
    if (hasKeyPage) {
        const QString url = info->keyPage;
        m_keyLink->setText(u"<a href=\"%1\">%2</a>"_s.arg(url, tr("Get an API key →")));
    }

    if (info && !info->envVar.isEmpty()) {
        m_envNote->setText(
            tr("Keys are stored locally and only sent to the provider's API endpoint.\n"
               "%1 overrides the stored key.")
                .arg(info->envVar));
    } else {
        m_envNote->setText(tr("Keys are stored locally and only sent to the configured base URL."));
    }
}

void SettingsDialog::setSettings(const AppSettings &settings) {
    m_drafts = settings.perProvider;
    const int index = m_providerCombo->findData(settings.provider);
    m_providerCombo->setCurrentIndex(index >= 0 ? index : 0);
    // Load fields explicitly: the index may already have been current.
    loadFieldsFor(m_providerCombo->currentData().toString());

    int langIndex = m_languageCombo->findData(settings.targetLanguage);
    if (langIndex < 0)
        langIndex = m_languageCombo->findData(u"zh-CN"_s);
    m_languageCombo->setCurrentIndex(langIndex >= 0 ? langIndex : 0);
    m_excludedAppsEdit->setText(settings.excludedApps.join(u", "_s));
    m_autoUpdateCheck->setChecked(settings.autoUpdate);
}

AppSettings SettingsDialog::settings() const {
    // settings() is const; the stash mutates drafts through a copy.
    AppSettings s;
    s.provider = m_providerCombo->currentData().toString();
    s.perProvider = m_drafts;
    ProviderSettings &current = s.perProvider[s.provider];
    current.apiKey = m_apiKeyEdit->text().trimmed();
    current.model = m_modelCombo->currentText().trimmed();
    current.baseUrl = m_baseUrlEdit->text().trimmed();
    while (current.baseUrl.endsWith(u'/'))
        current.baseUrl.chop(1);

    s.targetLanguage = m_languageCombo->currentData().toString();
    s.excludedApps = parseExcludedApps(m_excludedAppsEdit->text());
    s.autoUpdate = m_autoUpdateCheck->isChecked();
    return s;
}
