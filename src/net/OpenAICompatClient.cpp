#include "OpenAICompatClient.h"
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace arceus {

OpenAICompatClient::OpenAICompatClient(QObject *parent) : ApiClient(parent) {}

void OpenAICompatClient::cancel() {
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
}

static QJsonArray toOAIMessages(const QVector<Message> &msgs, const QString &sys)
{
    QJsonArray out;
    if (!sys.isEmpty()) {
        QJsonObject m; m["role"] = "system"; m["content"] = sys; out.append(m);
    }
    for (const auto &m : msgs) {
        QJsonObject jm;
        jm["role"] = roleToString(m.role);
        if (m.role == Role::Tool) {
            jm["tool_call_id"] = m.toolCallId;
            jm["content"] = m.content;
        } else if (m.role == Role::Assistant && !m.toolName.isEmpty()) {
            QJsonArray tc;
            QJsonObject one;
            one["id"] = m.toolCallId.isEmpty() ? "call_1" : m.toolCallId;
            one["type"] = "function";
            QJsonObject fn;
            fn["name"] = m.toolName;
            fn["arguments"] = QString::fromUtf8(QJsonDocument(m.toolArgs).toJson(QJsonDocument::Compact));
            one["function"] = fn;
            tc.append(one);
            jm["tool_calls"] = tc;
            jm["content"] = m.content;
        } else {
            jm["content"] = m.content;
        }
        out.append(jm);
    }
    return out;
}

void OpenAICompatClient::sendChat(const ChatRequest &req)
{
    if (!req.provider) { emit error("No provider"); return; }
    m_accumulated.clear();
    m_buffer.clear();
    m_toolCallEmitted = false;
    m_pendingToolCall = {};

    QString url = req.provider->baseUrl;
    if (!url.endsWith("/")) url += "/";
    url += "chat/completions";

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!req.provider->apiKey.isEmpty())
        nr.setRawHeader("Authorization", ("Bearer " + req.provider->apiKey).toUtf8());

    QJsonObject body;
    body["model"] = req.model;
    body["stream"] = req.stream;
    body["temperature"] = req.preset ? req.preset->temperature : 0.7;
    body["max_tokens"] = req.preset ? req.preset->maxTokens : 4096;
    body["messages"] = toOAIMessages(req.messages,
        req.systemOverride.isEmpty() && req.preset ? req.preset->systemPrompt : req.systemOverride);

    if (req.enableTools && req.preset && req.preset->enableTools) {
        body["tools"] = ToolRunner::schemasOpenAI();
    }

    m_reply = m_nam.post(nr, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAICompatClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAICompatClient::onFinished);
}

void OpenAICompatClient::onReadyRead()
{
    if (!m_reply) return;
    m_buffer.append(m_reply->readAll());

    int idx;
    while ((idx = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);
        if (line.isEmpty()) continue;
        if (!line.startsWith("data:")) continue;
        QByteArray payload = line.mid(5).trimmed();
        if (payload == "[DONE]") continue;

        const auto obj = QJsonDocument::fromJson(payload).object();
        const auto choices = obj.value("choices").toArray();
        if (choices.isEmpty()) continue;
        const auto delta = choices.first().toObject().value("delta").toObject();

        if (delta.contains("tool_calls")) {
            const auto tcs = delta.value("tool_calls").toArray();
            if (!tcs.isEmpty()) {
                const auto tc = tcs.first().toObject();
                const auto fn = tc.value("function").toObject();
                if (tc.contains("id")) m_pendingToolCall["id"] = tc.value("id").toString();
                if (fn.contains("name")) m_pendingToolCall["name"] = fn.value("name").toString();
                if (fn.contains("arguments")) {
                    QString acc = m_pendingToolCall.value("arguments").toString();
                    acc += fn.value("arguments").toString();
                    m_pendingToolCall["arguments"] = acc;
                }
            }
        }

        const QString content = delta.value("content").toString();
        if (!content.isEmpty()) {
            m_accumulated += content;
            emit deltaText(content);
        }
    }
}

void OpenAICompatClient::onFinished()
{
    if (!m_reply) return;
    const auto err = m_reply->error();
    const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray rest = m_reply->readAll();

    if (!rest.isEmpty() && m_buffer.isEmpty() && m_accumulated.isEmpty() &&
        m_pendingToolCall.isEmpty())
    {
        const auto doc = QJsonDocument::fromJson(rest);
        const auto choices = doc.object().value("choices").toArray();
        if (!choices.isEmpty()) {
            const auto msg = choices.first().toObject().value("message").toObject();
            const QString c = msg.value("content").toString();
            if (!c.isEmpty()) { m_accumulated = c; emit deltaText(c); }
            const auto tcs = msg.value("tool_calls").toArray();
            if (!tcs.isEmpty()) {
                const auto tc = tcs.first().toObject();
                const auto fn = tc.value("function").toObject();
                m_pendingToolCall["id"] = tc.value("id").toString();
                m_pendingToolCall["name"] = fn.value("name").toString();
                m_pendingToolCall["arguments"] = fn.value("arguments").toString();
            }
        } else if (doc.object().contains("error")) {
            emit error(doc.object().value("error").toObject().value("message").toString());
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
        const auto raw = m_pendingToolCall.value("arguments").toString();
        call.arguments = QJsonDocument::fromJson(raw.toUtf8()).object();
        emit toolCall(call);
    }

    emit finished(m_accumulated);
    m_reply->deleteLater();
    m_reply = nullptr;
}

}
