#include "Updater.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

using namespace Qt::StringLiterals;

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_installPath(QCoreApplication::applicationFilePath()) { }

bool Updater::isAppImage() {
    return qEnvironmentVariableIsSet("APPIMAGE");
}

bool Updater::isNewer(const QString &latest, const QString &current) {
    const auto parts = [](QString version) {
        if (version.startsWith(u'v'))
            version.remove(0, 1);
        QList<int> out;
        for (const QString &part : version.split(u'.'))
            out << part.toInt();
        while (out.size() < 3)
            out << 0;
        return out;
    };
    return parts(latest) > parts(current);
}

void Updater::check() {
    QNetworkRequest request { QUrl(QLatin1StringView(kLatestReleaseApi)) };
    // GitHub's API rejects requests without a User-Agent.
    request.setRawHeader("User-Agent", "translator/" TRANSLATOR_VERSION);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onCheckFinished(reply); });
}

void Updater::onCheckFinished(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(tr("Update check failed: %1").arg(reply->errorString()));
        return;
    }

    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString latest = release["tag_name"_L1].toString();
    if (latest.isEmpty()) {
        emit failed(tr("Update check failed: unexpected API response."));
        return;
    }
    if (!isNewer(latest, QStringLiteral(TRANSLATOR_VERSION))) {
        emit upToDate();
        return;
    }

    QString url;
    const QJsonArray assets = release["assets"_L1].toArray();
    for (const auto &asset : assets) {
        const QJsonObject obj = asset.toObject();
        if (obj["name"_L1].toString() == QLatin1StringView(kAppImageAssetName)) {
            url = obj["browser_download_url"_L1].toString();
            break;
        }
    }
    if (url.isEmpty()) {
        emit failed(tr("Release %1 has no AppImage asset.").arg(latest));
        return;
    }
    emit updateAvailable(latest, url);
}

void Updater::downloadAndInstall(const QString &url) {
    const QString partPath = m_installPath + ".part"_L1;
    m_downloadFile = new QFile(partPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit failed(tr("Cannot write to %1").arg(partPath));
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setRawHeader("User-Agent", "translator/" TRANSLATOR_VERSION);
    // GitHub asset URLs redirect to the CDN; follow them.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_download = m_nam->get(request);
    connect(m_download, &QNetworkReply::readyRead, this,
        [this] { m_downloadFile->write(m_download->readAll()); });
    connect(m_download, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
    connect(m_download, &QNetworkReply::finished, this, &Updater::onDownloadFinished);
}

void Updater::cancelDownload() {
    if (m_download)
        m_download->abort();
}

void Updater::onDownloadFinished() {
    m_downloadFile->write(m_download->readAll());
    m_downloadFile->flush();
    const QString partPath = m_downloadFile->fileName();
    m_downloadFile->close();

    const QNetworkReply::NetworkError error = m_download->error();
    m_download->deleteLater();
    m_download = nullptr;

    if (error != QNetworkReply::NoError) {
        m_downloadFile->remove();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
        emit failed(tr("Download failed."));
        return;
    }

    // Atomic replace: rename over the running image on the same filesystem,
    // then restore the executable bit.
    QFile::remove(m_installPath);
    if (!QFile::rename(partPath, m_installPath)) {
        emit failed(tr("Could not replace %1").arg(m_installPath));
        return;
    }
    QFile::setPermissions(m_installPath, QFileInfo(m_installPath).permissions() | QFile::ExeOwner);
    emit installed(m_installPath);
}
