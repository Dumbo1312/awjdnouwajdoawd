#pragma once
#include <QString>
#include <QJsonObject>

namespace arceus {

struct Preset {
    QString id;
    QString name;
    QString icon;
    QString systemPrompt;
    QString defaultProviderId;
    QString defaultModel;
    bool enableTools = true;
    bool enableImages = false;
    double temperature = 0.7;
    int maxTokens = 4096;

    QJsonObject toJson() const;
    static Preset fromJson(const QJsonObject &o);
};

}
