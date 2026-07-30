#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;

// One installed application found via its .desktop file.
struct InstalledApp {
    QString name; // display name ("KeePassXC")
    QString iconName; // themed icon name, may be empty
    // Candidate WM_CLASS / app-id values: StartupWMClass (when declared)
    // plus the desktop file id (what GTK4/Wayland apps report).
    QStringList wmClasses;
};

// Checklist of installed applications (parsed from .desktop files), used to
// fill Settings' "Exclude apps" field without hand-typing WM_CLASS names.
class AppPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit AppPickerDialog(QWidget *parent = nullptr);

    // WM_CLASS candidates of the apps checked in the dialog.
    [[nodiscard]] QStringList selectedWmClasses() const;
    // Pre-checks apps whose candidates appear in the given list.
    void setCheckedWmClasses(const QStringList &classes);

    // Enumerates installed apps from .desktop files in the given dirs
    // (empty = the system application dirs). Static for tests.
    static QList<InstalledApp> installedApps(const QStringList &dirs = { });

private:
    QList<InstalledApp> m_apps;
    QLineEdit *m_filterEdit;
    QListWidget *m_list;
};
