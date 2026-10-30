#pragma once
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

namespace arceus {

enum class PermissionMode {
    Ask,
    AcceptEdits,
    Bypass,
    ReadOnly,
};

QString permissionModeToString(PermissionMode m);
PermissionMode permissionModeFromString(const QString &s);

struct ToolCall {
    QString id;
    QString name;
    QJsonObject arguments;
};

struct ToolResult {
    QString callId;
    QString name;
    QString content;
    bool isError = false;
};

using AskUserFn = std::function<bool(const ToolCall &call, QString *reason)>;

class ToolRunner {
public:
    ToolRunner(QString workspace, PermissionMode mode, AskUserFn asker);

    ToolResult run(const ToolCall &call);

    static QJsonArray schemasOpenAI();
    static QJsonArray schemasAnthropic();
    static QJsonArray schemasGemini();

private:
    bool allowWrite(const ToolCall &call);
    QString resolvePath(const QString &p) const;

    QString m_workspace;
    PermissionMode m_mode;
    AskUserFn m_ask;
};

}
