#include "Chat.h"
#include <QJsonDocument>

namespace arceus {

QJsonObject Attachment::toJson() const {
    QJsonObject o;
    o["path"] = path;
    o["mime"] = mime;
    o["data"] = QString::fromLatin1(data.toBase64());
    return o;
}
Attachment Attachment::fromJson(const QJsonObject &o) {
    Attachment a;
    a.path = o.value("path").toString();
    a.mime = o.value("mime").toString();
    a.data = QByteArray::fromBase64(o.value("data").toString().toLatin1());
    return a;
}

QString roleToString(Role r) {
    switch (r) {
    case Role::System: return "system";
    case Role::User: return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool: return "tool";
    }
    return "user";
}
Role roleFromString(const QString &s) {
    if (s == "system") return Role::System;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool") return Role::Tool;
    return Role::User;
}

QJsonObject Message::toJson() const {
    QJsonObject o;
    o["role"] = roleToString(role);
    o["content"] = content;
    if (!toolCallId.isEmpty()) o["tool_call_id"] = toolCallId;
    if (!toolName.isEmpty()) o["tool_name"] = toolName;
    if (!toolArgs.isEmpty()) o["tool_args"] = toolArgs;
    QJsonArray arr;
    for (const auto &a : attachments) arr.append(a.toJson());
    if (!arr.isEmpty()) o["attachments"] = arr;
    o["created_at"] = createdAt.toString(Qt::ISODate);
    return o;
}
Message Message::fromJson(const QJsonObject &o) {
    Message m;
    m.role = roleFromString(o.value("role").toString());
    m.content = o.value("content").toString();
    m.toolCallId = o.value("tool_call_id").toString();
    m.toolName = o.value("tool_name").toString();
    m.toolArgs = o.value("tool_args").toObject();
    for (const auto &v : o.value("attachments").toArray())
        m.attachments.append(Attachment::fromJson(v.toObject()));
    m.createdAt = QDateTime::fromString(o.value("created_at").toString(), Qt::ISODate);
    if (!m.createdAt.isValid()) m.createdAt = QDateTime::currentDateTime();
    return m;
}

QJsonObject Chat::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["title"] = title;
    o["preset_id"] = presetId;
    o["provider_id"] = providerId;
    o["model"] = model;
    o["permission_mode"] = permissionMode;
    o["updated_at"] = updatedAt.toString(Qt::ISODate);
    QJsonArray arr;
    for (const auto &m : messages) arr.append(m.toJson());
    o["messages"] = arr;
    return o;
}
Chat Chat::fromJson(const QJsonObject &o) {
    Chat c;
    c.id = o.value("id").toString();
    c.title = o.value("title").toString();
    c.presetId = o.value("preset_id").toString();
    c.providerId = o.value("provider_id").toString();
    c.model = o.value("model").toString();
    c.permissionMode = o.value("permission_mode").toString("ask");
    c.updatedAt = QDateTime::fromString(o.value("updated_at").toString(), Qt::ISODate);
    if (!c.updatedAt.isValid()) c.updatedAt = QDateTime::currentDateTime();
    for (const auto &v : o.value("messages").toArray())
        c.messages.append(Message::fromJson(v.toObject()));
    return c;
}

}
