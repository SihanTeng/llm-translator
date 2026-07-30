#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;

// Persisted application settings (BYOK: the user's own DeepSeek API key,
// stored locally via QSettings, never sent anywhere except the configured
// base URL). The DEEPSEEK_API_KEY environment variable takes precedence
// over the stored key.
struct AppSettings {
    QString apiKey;
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString model = QStringLiteral("deepseek-v4-flash");
    QString targetLanguage = QStringLiteral("zh"); // zh | en | auto
    // WM_CLASS names that should never trigger the Translate bar (GNOME
    // Wayland only — X11 selections carry no source-app information).
    QStringList excludedApps;
    bool autoUpdate = true;

    static AppSettings load();
    void save() const;

    // DEEPSEEK_API_KEY overrides the key stored in settings.
    QString effectiveApiKey() const;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setSettings(const AppSettings &settings);
    [[nodiscard]] AppSettings settings() const;

private:
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_baseUrlEdit;
    QComboBox *m_modelCombo;
    QComboBox *m_languageCombo;
    QLineEdit *m_excludedAppsEdit;
    QCheckBox *m_autoUpdateCheck;
};
