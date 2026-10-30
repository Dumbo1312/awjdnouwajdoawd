#include "Tools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>

namespace arceus {

QString permissionModeToString(PermissionMode m) {
    switch (m) {
    case PermissionMode::Ask: return "ask";
    case PermissionMode::AcceptEdits: return "accept_edits";
    case PermissionMode::Bypass: return "bypass";
    case PermissionMode::ReadOnly: return "read_only";
    }
    return "ask";
}

PermissionMode permissionModeFromString(const QString &s) {
    if (s == "accept_edits") return PermissionMode::AcceptEdits;
    if (s == "bypass") return PermissionMode::Bypass;
    if (s == "read_only") return PermissionMode::ReadOnly;
    return PermissionMode::Ask;
}

ToolRunner::ToolRunner(QString workspace, PermissionMode mode, AskUserFn asker)
    : m_workspace(std::move(workspace)), m_mode(mode), m_ask(std::move(asker)) {}

QString ToolRunner::resolvePath(const QString &p) const
{
    QString path = p;
    if (path.isEmpty()) return m_workspace;
    QFileInfo fi(path);
    if (!fi.isAbsolute()) path = m_workspace + "/" + path;
    return QDir::cleanPath(path);
}

bool ToolRunner::allowWrite(const ToolCall &call)
{
    if (m_mode == PermissionMode::Bypass) return true;
    if (m_mode == PermissionMode::AcceptEdits &&
        (call.name == "write_file" || call.name == "read_file" ||
         call.name == "list_dir" || call.name == "edit_file")) return true;
    if (m_mode == PermissionMode::ReadOnly &&
        (call.name == "read_file" || call.name == "list_dir")) return true;
    if (!m_ask) return false;
    QString reason;
    return m_ask(call, &reason);
}

ToolResult ToolRunner::run(const ToolCall &call)
{
    ToolResult r;
    r.callId = call.id;
    r.name = call.name;

    if (call.name == "read_file") {
        if (m_mode == PermissionMode::Ask && !allowWrite(call)) {
            r.isError = true; r.content = "User denied read_file"; return r;
        }
        const QString path = resolvePath(call.arguments.value("path").toString());
        QFile f(path);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            r.isError = true;
            r.content = QString("Could not open %1: %2").arg(path, f.errorString());
            return r;
        }
        r.content = QString::fromUtf8(f.readAll());
        return r;
    }

    if (call.name == "list_dir") {
        if (m_mode == PermissionMode::Ask && !allowWrite(call)) {
            r.isError = true; r.content = "User denied list_dir"; return r;
        }
        const QString path = resolvePath(call.arguments.value("path").toString());
        QDir d(path);
        if (!d.exists()) {
            r.isError = true;
            r.content = "No such directory: " + path;
            return r;
        }
        QStringList items;
        for (const QFileInfo &fi : d.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
                                                  QDir::DirsFirst | QDir::Name)) {
            items << (fi.isDir() ? fi.fileName() + "/" : fi.fileName());
        }
        r.content = items.join("\n");
        return r;
    }

    if (call.name == "write_file") {
        if (!allowWrite(call)) {
            r.isError = true; r.content = "User denied write_file"; return r;
        }
        const QString path = resolvePath(call.arguments.value("path").toString());
        const QString content = call.arguments.value("content").toString();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
            r.isError = true;
            r.content = "Could not write " + path + ": " + f.errorString();
            return r;
        }
        f.write(content.toUtf8());
        r.content = "Wrote " + QString::number(content.size()) + " chars to " + path;
        return r;
    }

    if (call.name == "edit_file") {
        if (!allowWrite(call)) {
            r.isError = true; r.content = "User denied edit_file"; return r;
        }
        const QString path = resolvePath(call.arguments.value("path").toString());
        const QString oldStr = call.arguments.value("old_str").toString();
        const QString newStr = call.arguments.value("new_str").toString();
        QFile f(path);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            r.isError = true; r.content = "Could not open " + path; return r;
        }
        QString body = QString::fromUtf8(f.readAll());
        f.close();
        if (!body.contains(oldStr)) {
            r.isError = true; r.content = "old_str not found in " + path; return r;
        }
        body.replace(oldStr, newStr);
        if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
            r.isError = true; r.content = "Could not write " + path; return r;
        }
        f.write(body.toUtf8());
        r.content = "Edited " + path;
        return r;
    }

    if (call.name == "run_command") {
        if (!allowWrite(call)) {
            r.isError = true; r.content = "User denied run_command"; return r;
        }
        const QString cmd = call.arguments.value("command").toString();
        QProcess p;
        p.setWorkingDirectory(m_workspace);
#ifdef Q_OS_WIN
        p.setProgram("cmd.exe");
        p.setArguments({ "/C", cmd });
#else
        p.setProgram("/bin/sh");
        p.setArguments({ "-c", cmd });
#endif
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        if (!p.waitForStarted(5000)) {
            r.isError = true; r.content = "Failed to start process"; return r;
        }
        p.waitForFinished(120000);
        QString out = QString::fromUtf8(p.readAll());
        if (out.size() > 20000) out = out.left(20000) + "\n…[truncated]";
        r.content = QString("exit=%1\n%2").arg(p.exitCode()).arg(out);
        return r;
    }

    if (call.name == "generate_image") {
        r.content = "generate_image is handled by the image provider; the UI will render the result.";
        return r;
    }

    r.isError = true;
    r.content = "Unknown tool: " + call.name;
    return r;
}

static QJsonObject prop(const QString &type, const QString &desc) {
    QJsonObject p; p["type"] = type; p["description"] = desc; return p;
}

static QJsonObject objSchema(const QJsonObject &props, const QStringList &req) {
    QJsonObject o;
    o["type"] = "object";
    o["properties"] = props;
    QJsonArray r; for (const auto &k : req) r.append(k);
    o["required"] = r;
    return o;
}

QJsonArray ToolRunner::schemasOpenAI()
{
    auto make = [](const char *name, const char *desc, const QJsonObject &params) {
        QJsonObject t;
        t["type"] = "function";
        QJsonObject fn;
        fn["name"] = name;
        fn["description"] = desc;
        fn["parameters"] = params;
        t["function"] = fn;
        return t;
    };

    QJsonArray arr;

    {
        QJsonObject p;
        p["path"] = prop("string", "File path, relative to the workspace or absolute.");
        arr.append(make("read_file", "Read a text file.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "Directory path.");
        arr.append(make("list_dir", "List files in a directory.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["content"] = prop("string", "Full new file contents.");
        arr.append(make("write_file", "Create or overwrite a text file.", objSchema(p, {"path", "content"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["old_str"] = prop("string", "Exact substring to replace. Must appear in the file.");
        p["new_str"] = prop("string", "Replacement.");
        arr.append(make("edit_file", "Replace a substring in a file.", objSchema(p, {"path", "old_str", "new_str"})));
    }
    {
        QJsonObject p;
        p["command"] = prop("string", "Shell command to run in the workspace.");
        arr.append(make("run_command", "Run a shell command.", objSchema(p, {"command"})));
    }
    {
        QJsonObject p;
        p["prompt"] = prop("string", "Image prompt.");
        arr.append(make("generate_image", "Generate an image from a prompt.", objSchema(p, {"prompt"})));
    }

    return arr;
}

QJsonArray ToolRunner::schemasAnthropic()
{
    QJsonArray arr;
    auto make = [](const char *name, const char *desc, const QJsonObject &params) {
        QJsonObject t;
        t["name"] = name;
        t["description"] = desc;
        t["input_schema"] = params;
        return t;
    };
    {
        QJsonObject p; p["path"] = prop("string", "File path.");
        arr.append(make("read_file", "Read a text file.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p; p["path"] = prop("string", "Directory path.");
        arr.append(make("list_dir", "List files.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["content"] = prop("string", "Full new contents.");
        arr.append(make("write_file", "Create or overwrite a text file.", objSchema(p, {"path","content"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["old_str"] = prop("string", "Exact substring.");
        p["new_str"] = prop("string", "Replacement.");
        arr.append(make("edit_file", "Replace a substring in a file.", objSchema(p, {"path","old_str","new_str"})));
    }
    {
        QJsonObject p; p["command"] = prop("string", "Shell command.");
        arr.append(make("run_command", "Run a shell command.", objSchema(p, {"command"})));
    }
    {
        QJsonObject p; p["prompt"] = prop("string", "Image prompt.");
        arr.append(make("generate_image", "Generate an image from a prompt.", objSchema(p, {"prompt"})));
    }
    return arr;
}

QJsonArray ToolRunner::schemasGemini()
{
    QJsonArray decls;
    auto make = [](const char *name, const char *desc, const QJsonObject &params) {
        QJsonObject t;
        t["name"] = name;
        t["description"] = desc;
        t["parameters"] = params;
        return t;
    };
    {
        QJsonObject p; p["path"] = prop("string", "File path.");
        decls.append(make("read_file", "Read a text file.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p; p["path"] = prop("string", "Directory path.");
        decls.append(make("list_dir", "List files.", objSchema(p, {"path"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["content"] = prop("string", "Full new contents.");
        decls.append(make("write_file", "Create or overwrite a text file.", objSchema(p, {"path","content"})));
    }
    {
        QJsonObject p;
        p["path"] = prop("string", "File path.");
        p["old_str"] = prop("string", "Exact substring.");
        p["new_str"] = prop("string", "Replacement.");
        decls.append(make("edit_file", "Replace a substring in a file.", objSchema(p, {"path","old_str","new_str"})));
    }
    {
        QJsonObject p; p["command"] = prop("string", "Shell command.");
        decls.append(make("run_command", "Run a shell command.", objSchema(p, {"command"})));
    }
    QJsonObject tool;
    tool["function_declarations"] = decls;
    QJsonArray tools; tools.append(tool);
    return tools;
}

}
