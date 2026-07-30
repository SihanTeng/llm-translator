#pragma once

#include <QDialog>
#include <QHash>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

// Per-provider credentials/config, stored in QSettings groups keyed by
// provider id ([deepseek], [openai], ... — see Provider.h).
struct ProviderSettings {
    QString apiKey;
    QString model;
    QString baseUrl; // editable only for the "custom" provider
};

// Persisted application settings (BYOK: keys are stored locally via
// QSettings and only ever sent to that provider's API endpoint). Each
// provider additionally honors its conventional env var (DEEPSEEK_API_KEY,
// OPENAI_API_KEY, ...), which overrides the stored key and is never
// written to disk.
struct AppSettings {
    QString provider = QStringLiteral("deepseek");
    QHash<QString, ProviderSettings> perProvider;
    QString targetLanguage = QStringLiteral("zh-CN"); // language code, see Languages.h
    // WM_CLASS names that should never trigger the Translate bar (GNOME
    // Wayland only — X11 selections carry no source-app information).
    QStringList excludedApps;
    bool autoUpdate = true;

    static AppSettings load();
    void save() const;

    [[nodiscard]] ProviderSettings currentProviderSettings() const;
    // The current provider's env var wins over its stored key.
    [[nodiscard]] QString effectiveApiKey() const;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setSettings(const AppSettings &settings);
    [[nodiscard]] AppSettings settings() const;

private:
    void onProviderChanged(int index);
    void stashCurrentFields();
    void loadFieldsFor(const QString &providerId);

    QComboBox *m_providerCombo;
    QLineEdit *m_apiKeyEdit;
    QComboBox *m_modelCombo;
    QLabel *m_baseUrlLabel;
    QLineEdit *m_baseUrlEdit;
    QComboBox *m_languageCombo;
    QLineEdit *m_excludedAppsEdit;
    QCheckBox *m_autoUpdateCheck;
    QLabel *m_keyLink;
    QLabel *m_envNote;
    QString m_currentProviderId;
    // Unsaved per-provider field values while the user switches providers.
    QHash<QString, ProviderSettings> m_drafts;
};
