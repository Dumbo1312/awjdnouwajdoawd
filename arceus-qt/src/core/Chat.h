#pragma once
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

namespace arceus {

struct Attachment {
    QString path;
    QString mime;
    QByteArray data;
    QJsonObject toJson() const;
    static Attachment fromJson(const QJsonObject &o);
};

enum class Role { System, User, Assistant, Tool };
QString roleToString(Role r);
Role roleFromString(const QString &s);

struct Message {
    Role role = Role::User;
    QString content;
    QString toolCallId;
    QString toolName;
    QJsonObject toolArgs;
    QVector<Attachment> attachments;
    QDateTime createdAt;
    QJsonObject toJson() const;
    static Message fromJson(const QJsonObject &o);
};

struct Chat {
    QString id;
    QString title;
    QString presetId;
    QString providerId;
    QString model;
    QString permissionMode;
    QVector<Message> messages;
    QDateTime updatedAt;
    QJsonObject toJson() const;
    static Chat fromJson(const QJsonObject &o);
};

}
