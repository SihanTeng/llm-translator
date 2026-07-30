#include "Speaker.h"

#include <QProcess>
#include <QStandardPaths>
#include <QtDebug>

Speaker::Speaker(QObject *parent)
    : QObject(parent) { }

bool Speaker::isAvailable() {
    return !QStandardPaths::findExecutable(QStringLiteral("spd-say")).isEmpty();
}

void Speaker::speak(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (!isAvailable()) {
        qWarning() << "TTS: spd-say not found (install speech-dispatcher)";
        return;
    }
    stop();
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
    // "--": selections may start with a dash, which spd-say would parse as
    // options.
    m_process->start(QStringLiteral("spd-say"), { QStringLiteral("--"), trimmed });
}

void Speaker::stop() {
    if (!m_process)
        return;
    m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
}
