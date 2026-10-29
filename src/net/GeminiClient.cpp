#include "GeminiClient.h"
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace arceus {

GeminiClient::GeminiClient(QObject *parent) : ApiClient(parent) {}

void GeminiClient::cancel() {
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
}

static QJsonArray toContents(const QVector<Message> &msgs)
{
    QJsonArray out;
    for (const auto &m : msgs) {
        if (m.role == Role::System) continue;
        QJsonObject jm;
        jm["role"] = (m.role == Role::Assistant) ? "model" : "user";
        QJsonArray parts;
        if (m.role == Role::Tool) {
            QJsonObject fr;
            QJsonObject fnResp;
            fnResp["name"] = m.toolName;
            QJsonObject resp;
            resp["content"] = m.content;
            fnResp["response"] = resp;
            fr["functionResponse"] = fnResp;
            parts.append(fr);
        } else {
            QJsonObject t; t["text"] = m.content; parts.append(t);
            if (m.role == Role::Assistant && !m.toolName.isEmpty()) {
                QJsonObject fc;
                QJsonObject call;
                call["name"] = m.toolName;
                call["args"] = m.toolArgs;
                fc["functionCall"] = call;
                parts.append(fc);
            }
        }
        jm["parts"] = parts;
        out.append(jm);
    }
    return out;
}

void GeminiClient::sendChat(const ChatRequest &req)
{
    if (!req.provider) { emit error("No provider"); return; }

    QString url = req.provider->baseUrl;
    if (!url.endsWith("/")) url += "/";
    url += "v1beta/models/" + req.model + ":generateContent";

    QUrl u(url);
    QUrlQuery q;
    q.addQueryItem("key", req.provider->apiKey);
    u.setQuery(q);

    QNetworkRequest nr{u};
    nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["contents"] = toContents(req.messages);
    QJsonObject gen;
    gen["temperature"] = req.preset ? req.preset->temperature : 0.7;
    gen["maxOutputTokens"] = req.preset ? req.preset->maxTokens : 4096;
    body["generationConfig"] = gen;
    const QString sys = req.systemOverride.isEmpty() && req.preset ? req.preset->systemPrompt : req.systemOverride;
    if (!sys.isEmpty()) {
        QJsonObject si; QJsonArray p;
        QJsonObject t; t["text"] = sys; p.append(t);
        si["parts"] = p;
        body["systemInstruction"] = si;
    }
    if (req.enableTools && req.preset && req.preset->enableTools)
        body["tools"] = ToolRunner::schemasGemini();

    m_reply = m_nam.post(nr, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &GeminiClient::onFinished);
}

void GeminiClient::onFinished()
{
    if (!m_reply) return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = m_reply->error();
    const QByteArray body = m_reply->readAll();

    if (err != QNetworkReply::NoError) {
        emit error(QString("HTTP %1: %2 — %3").arg(status).arg(m_reply->errorString(),
                                                               QString::fromUtf8(body).left(400)));
        m_reply->deleteLater(); m_reply = nullptr; return;
    }

    const auto doc = QJsonDocument::fromJson(body).object();
    if (doc.contains("error")) {
        emit error(doc.value("error").toObject().value("message").toString());
        m_reply->deleteLater(); m_reply = nullptr; return;
    }

    QString text;
    ToolCall call;
    for (const auto &c : doc.value("candidates").toArray()) {
        const auto parts = c.toObject().value("content").toObject().value("parts").toArray();
        for (const auto &pv : parts) {
            const auto po = pv.toObject();
            if (po.contains("text")) {
                const QString t = po.value("text").toString();
                text += t;
                emit deltaText(t);
            }
            if (po.contains("functionCall")) {
                const auto fc = po.value("functionCall").toObject();
                call.id = "gemfn_" + fc.value("name").toString();
                call.name = fc.value("name").toString();
                call.arguments = fc.value("args").toObject();
            }
        }
    }

    if (!call.name.isEmpty()) emit toolCall(call);
    emit finished(text);
    m_reply->deleteLater();
    m_reply = nullptr;
}

}
