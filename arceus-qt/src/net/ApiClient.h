#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QJsonObject>
#include "core/Chat.h"
#include "core/Preset.h"
#include "core/Provider.h"
#include "core/Tools.h"

namespace arceus {

struct ChatRequest {
    const Provider *provider = nullptr;
    const Preset *preset = nullptr;
    QString model;
    QVector<Message> messages;
    QString systemOverride;
    bool enableTools = false;
    bool stream = true;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~ApiClient() = default;

    virtual void sendChat(const ChatRequest &req) = 0;
    virtual void generateImage(const Provider *provider, const QString &model, const QString &prompt) {
        Q_UNUSED(provider); Q_UNUSED(model); Q_UNUSED(prompt);
        emit error("Image generation is not supported by this provider.");
    }
    virtual void cancel() {}

signals:
    void deltaText(const QString &chunk);
    void toolCall(const arceus::ToolCall &call);
    void finished(const QString &finalText);
    void imageReady(const QByteArray &bytes, const QString &mime);
    void error(const QString &message);
};

}
