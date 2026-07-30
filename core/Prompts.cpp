#include "Prompts.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {
QString fill(const QString &key, const QString &targetLanguageName) {
    static const QJsonObject prompts = [] {
        QFile file(QStringLiteral(":/spec/prompts.json"));
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(file.readAll())["prompts"_L1].toObject();
    }();
    return prompts[key].toString().replace("{target}"_L1, targetLanguageName);
}
} // namespace

QString Prompts::phrase(const QString &targetLanguageName) {
    return fill(QStringLiteral("phrase"), targetLanguageName);
}

QString Prompts::word(const QString &targetLanguageName) {
    return fill(QStringLiteral("word"), targetLanguageName);
}
