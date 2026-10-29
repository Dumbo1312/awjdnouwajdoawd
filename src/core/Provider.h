#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace arceus {

enum class ProviderKind {
    OpenAICompat,
    Anthropic,
    Gemini,
    HuggingFace,
};

QString providerKindToString(ProviderKind k);
ProviderKind providerKindFromString(const QString &s);

struct Provider {
    QString id;
    QString name;
    ProviderKind kind = ProviderKind::OpenAICompat;
    QString baseUrl;
    QString apiKey;
    QStringList models;
    bool supportsImages = false;
    bool enabled = true;
    bool builtin = false;

    QJsonObject toJson() const;
    static Provider fromJson(const QJsonObject &o);
};

}
