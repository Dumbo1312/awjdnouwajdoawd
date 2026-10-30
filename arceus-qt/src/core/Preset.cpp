#include "Preset.h"

namespace arceus {

QJsonObject Preset::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["icon"] = icon;
    o["system_prompt"] = systemPrompt;
    o["default_provider_id"] = defaultProviderId;
    o["default_model"] = defaultModel;
    o["enable_tools"] = enableTools;
    o["enable_images"] = enableImages;
    o["temperature"] = temperature;
    o["max_tokens"] = maxTokens;
    return o;
}

Preset Preset::fromJson(const QJsonObject &o) {
    Preset p;
    p.id = o.value("id").toString();
    p.name = o.value("name").toString();
    p.icon = o.value("icon").toString();
    p.systemPrompt = o.value("system_prompt").toString();
    p.defaultProviderId = o.value("default_provider_id").toString();
    p.defaultModel = o.value("default_model").toString();
    p.enableTools = o.value("enable_tools").toBool(true);
    p.enableImages = o.value("enable_images").toBool(false);
    p.temperature = o.value("temperature").toDouble(0.7);
    p.maxTokens = o.value("max_tokens").toInt(4096);
    return p;
}

}
