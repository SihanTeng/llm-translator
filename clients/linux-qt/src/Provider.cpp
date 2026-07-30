#include "Provider.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {
ApiStyle parseStyle(const QString &style) {
    return style == "anthropic"_L1 ? ApiStyle::Anthropic : ApiStyle::OpenAiCompatible;
}

JsonMode parseJsonMode(const QString &mode) {
    return mode == "prompt_only"_L1 ? JsonMode::PromptOnly : JsonMode::ResponseFormat;
}

QList<ProviderInfo> loadProviders() {
    QFile file(QStringLiteral(":/spec/providers.json"));
    QList<ProviderInfo> list;
    if (!file.open(QIODevice::ReadOnly))
        return list;
    const QJsonArray entries = QJsonDocument::fromJson(file.readAll())["providers"_L1].toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject obj = value.toObject();
        ProviderInfo info;
        info.id = obj["id"_L1].toString();
        info.name = obj["name"_L1].toString();
        info.style = parseStyle(obj["style"_L1].toString());
        info.jsonMode = parseJsonMode(obj["jsonMode"_L1].toString());
        info.baseUrl = obj["baseUrl"_L1].toString();
        info.defaultModel = obj["defaultModel"_L1].toString();
        info.altModel = obj["altModel"_L1].toString();
        info.envVar = obj["envVar"_L1].toString();
        info.keyPage = obj["keyPage"_L1].toString();
        info.disableThinking = obj["disableThinking"_L1].toBool();
        list.append(info);
    }
    return list;
}
} // namespace

const QList<ProviderInfo> &providers() {
    static const QList<ProviderInfo> list = loadProviders();
    return list;
}

const ProviderInfo *providerById(const QString &id) {
    for (const ProviderInfo &info : providers()) {
        if (id == info.id)
            return &info;
    }
    return nullptr;
}
