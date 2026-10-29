#include "Provider.h"
#include <QJsonArray>

namespace arceus {

QString providerKindToString(ProviderKind k) {
    switch (k) {
    case ProviderKind::OpenAICompat: return "openai_compat";
    case ProviderKind::Anthropic: return "anthropic";
    case ProviderKind::Gemini: return "gemini";
    case ProviderKind::HuggingFace: return "huggingface";
    }
    return "openai_compat";
}
ProviderKind providerKindFromString(const QString &s) {
    if (s == "anthropic") return ProviderKind::Anthropic;
    if (s == "gemini") return ProviderKind::Gemini;
    if (s == "huggingface") return ProviderKind::HuggingFace;
    return ProviderKind::OpenAICompat;
}

QJsonObject Provider::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["kind"] = providerKindToString(kind);
    o["base_url"] = baseUrl;
    o["api_key"] = apiKey;
    QJsonArray arr;
    for (const auto &m : models) arr.append(m);
    o["models"] = arr;
    o["supports_images"] = supportsImages;
    o["enabled"] = enabled;
    o["builtin"] = builtin;
    return o;
}

Provider Provider::fromJson(const QJsonObject &o) {
    Provider p;
    p.id = o.value("id").toString();
    p.name = o.value("name").toString();
    p.kind = providerKindFromString(o.value("kind").toString());
    p.baseUrl = o.value("base_url").toString();
    p.apiKey = o.value("api_key").toString();
    for (const auto &v : o.value("models").toArray()) p.models.append(v.toString());
    p.supportsImages = o.value("supports_images").toBool(false);
    p.enabled = o.value("enabled").toBool(true);
    p.builtin = o.value("builtin").toBool(false);
    return p;
}

}
