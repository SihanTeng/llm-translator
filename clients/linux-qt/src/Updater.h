#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// OTA update checker/installer.
//
// check() queries the GitHub releases API for the latest release and emits
// updateAvailable() when it is newer than the running build. In AppImage
// mode, downloadAndInstall() fetches the new image and atomically replaces
// the running file; other install modes (rpm, dev build) should only notify
// and point at the release page — the package manager / dev loop owns them.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject *parent = nullptr);

    // True when running from an AppImage (the AppRun runtime sets APPIMAGE).
    static bool isAppImage();
    // Numeric semver-ish compare: "1.3.0" > "1.2". Tolerates a leading 'v'.
    static bool isNewer(const QString &latest, const QString &current);

    void setInstallPath(const QString &path) { m_installPath = path; } // tests

    void check();
    void downloadAndInstall(const QString &url);
    void cancelDownload();

signals:
    void updateAvailable(const QString &version, const QString &downloadUrl);
    void upToDate();
    void downloadProgress(qint64 received, qint64 total);
    void installed(const QString &path);
    void failed(const QString &message);

private:
    void onCheckFinished(QNetworkReply *reply);
    void onDownloadFinished();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_download = nullptr;
    QFile *m_downloadFile = nullptr;
    QString m_installPath;

    static constexpr const char *kLatestReleaseApi
        = "https://api.github.com/repos/SihanTeng/llm-translator/releases/latest";
    static constexpr const char *kAppImageAssetName = "translator-x86_64.AppImage";
};
