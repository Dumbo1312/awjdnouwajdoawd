#include "AnthropicClient.h"
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace arceus {

AnthropicClient::AnthropicClient(QObject *parent) : ApiClient(parent) {}

void AnthropicClient::cancel() {
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
}

static QJsonArray toAnthropicMessages(const QVector<Message> &msgs)
{
    QJsonArray out;
    for (const auto &m : msgs) {
        if (m.role == Role::System) continue;
        QJsonObject jm;
        if (m.role == Role::Tool) {
            jm["role"] = "user";
            QJsonArray content;
            QJsonObject block;
            block["type"] = "tool_result";
            block["tool_use_id"] = m.toolCallId;
            block["content"] = m.content;
            content.append(block);
            jm["content"] = content;
        } else if (m.role == Role::Assistant && !m.toolName.isEmpty()) {
            jm["role"] = "assistant";
            QJsonArray content;
            if (!m.content.isEmpty()) {
                QJsonObject t; t["type"] = "text"; t["text"] = m.content; content.append(t);
            }
            QJsonObject tu;
            tu["type"] = "tool_use";
            tu["id"] = m.toolCallId;
            tu["name"] = m.toolName;
            tu["input"] = m.toolArgs;
            content.append(tu);
            jm["content"] = content;
        } else {
            jm["role"] = roleToString(m.role);
            jm["content"] = m.content;
        }
        out.append(jm);
    }
    return out;
}

void AnthropicClient::sendChat(const ChatRequest &req)
{
    if (!req.provider) { emit error("No provider"); return; }
    m_accumulated.clear();
    m_buffer.clear();
    m_pendingToolCall = {};
    m_pendingToolInputJson.clear();

    QString url = req.provider->baseUrl;
    if (!url.endsWith("/")) url += "/";
    url += "v1/messages";

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    nr.setRawHeader("x-api-key", req.provider->apiKey.toUtf8());
    nr.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject body;
    body["model"] = req.model;
    body["max_tokens"] = req.preset ? req.preset->maxTokens : 4096;
    body["temperature"] = req.preset ? req.preset->temperature : 0.7;
    body["stream"] = req.stream;
    const QString sys = req.systemOverride.isEmpty() && req.preset ? req.preset->systemPrompt : req.systemOverride;
    if (!sys.isEmpty()) body["system"] = sys;
    body["messages"] = toAnthropicMessages(req.messages);
    if (req.enableTools && req.preset && req.preset->enableTools)
        body["tools"] = ToolRunner::schemasAnthropic();

    m_reply = m_nam.post(nr, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AnthropicClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &AnthropicClient::onFinished);
}

void AnthropicClient::onReadyRead()
{
    if (!m_reply) return;
    m_buffer.append(m_reply->readAll());
    int idx;
    while ((idx = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);
        if (!line.startsWith("data:")) continue;
        QByteArray payload = line.mid(5).trimmed();
        if (payload.isEmpty()) continue;

        const auto obj = QJsonDocument::fromJson(payload).object();
        const QString type = obj.value("type").toString();

        if (type == "content_block_start") {
            const auto cb = obj.value("content_block").toObject();
            if (cb.value("type").toString() == "tool_use") {
                m_pendingToolCall["id"] = cb.value("id").toString();
                m_pendingToolCall["name"] = cb.value("name").toString();
                m_pendingToolInputJson.clear();
            }
        } else if (type == "content_block_delta") {
            const auto d = obj.value("delta").toObject();
            const QString dtype = d.value("type").toString();
            if (dtype == "text_delta") {
                const QString t = d.value("text").toString();
                m_accumulated += t;
                emit deltaText(t);
            } else if (dtype == "input_json_delta") {
                m_pendingToolInputJson += d.value("partial_json").toString();
            }
        }
    }
}

void AnthropicClient::onFinished()
{
    if (!m_reply) return;
    const auto err = m_reply->error();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray rest = m_reply->readAll();

    if (!rest.isEmpty() && m_accumulated.isEmpty() && m_pendingToolCall.isEmpty()) {
        const auto doc = QJsonDocument::fromJson(rest).object();
        for (const auto &v : doc.value("content").toArray()) {
            const auto o = v.toObject();
            if (o.value("type").toString() == "text") {
                const QString t = o.value("text").toString();
                m_accumulated += t;
                emit deltaText(t);
            } else if (o.value("type").toString() == "tool_use") {
                m_pendingToolCall["id"] = o.value("id").toString();
                m_pendingToolCall["name"] = o.value("name").toString();
                m_pendingToolCall["input"] = o.value("input").toObject();
            }
        }
        if (doc.contains("error")) {
            emit error(doc.value("error").toObject().value("message").toString());
            m_reply->deleteLater(); m_reply = nullptr;
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        QString msg = m_reply->errorString();
        if (!rest.isEmpty()) msg += " — " + QString::fromUtf8(rest).left(400);
        emit error(QString("HTTP %1: %2").arg(status).arg(msg));
        m_reply->deleteLater(); m_reply = nullptr;
        return;
    }

    if (!m_pendingToolCall.value("name").toString().isEmpty()) {
        ToolCall call;
        call.id = m_pendingToolCall.value("id").toString();
        call.name = m_pendingToolCall.value("name").toString();
        if (m_pendingToolCall.contains("input")) {
            call.arguments = m_pendingToolCall.value("input").toObject();
        } else {
            call.arguments = QJsonDocument::fromJson(m_pendingToolInputJson.toUtf8()).object();
        }
        emit toolCall(call);
    }

    emit finished(m_accumulated);
    m_reply->deleteLater();
    m_reply = nullptr;
}

}
