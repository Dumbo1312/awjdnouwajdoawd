#include "HuggingFaceClient.h"
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace arceus {

HuggingFaceClient::HuggingFaceClient(QObject *parent) : ApiClient(parent) {}

void HuggingFaceClient::cancel() {
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
}

void HuggingFaceClient::sendChat(const ChatRequest &req)
{
    if (!req.provider) { emit error("No provider"); return; }

    QString url = req.provider->baseUrl;
    if (!url.endsWith("/")) url += "/";
    url += "models/" + req.model;

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!req.provider->apiKey.isEmpty())
        nr.setRawHeader("Authorization", ("Bearer " + req.provider->apiKey).toUtf8());

    QString joined;
    const QString sys = req.systemOverride.isEmpty() && req.preset ? req.preset->systemPrompt : req.systemOverride;
    if (!sys.isEmpty()) joined += "<|system|>\n" + sys + "\n";
    for (const auto &m : req.messages) {
        joined += "<|" + roleToString(m.role) + "|>\n" + m.content + "\n";
    }
    joined += "<|assistant|>\n";

    QJsonObject body;
    body["inputs"] = joined;
    QJsonObject params;
    params["temperature"] = req.preset ? req.preset->temperature : 0.7;
    params["max_new_tokens"] = req.preset ? req.preset->maxTokens : 2048;
    params["return_full_text"] = false;
    body["parameters"] = params;

    m_reply = m_nam.post(nr, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &HuggingFaceClient::onChatFinished);
}

void HuggingFaceClient::onChatFinished()
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

    const auto doc = QJsonDocument::fromJson(body);
    QString text;
    if (doc.isArray()) {
        for (const auto &v : doc.array()) {
            text += v.toObject().value("generated_text").toString();
        }
    } else if (doc.isObject()) {
        const auto o = doc.object();
        if (o.contains("error")) {
            emit error(o.value("error").toString());
            m_reply->deleteLater(); m_reply = nullptr; return;
        }
        text = o.value("generated_text").toString();
    }

    if (!text.isEmpty()) emit deltaText(text);
    emit finished(text);
    m_reply->deleteLater();
    m_reply = nullptr;
}

void HuggingFaceClient::generateImage(const Provider *provider, const QString &model, const QString &prompt)
{
    if (!provider) { emit error("No provider"); return; }

    QString url = provider->baseUrl;
    if (!url.endsWith("/")) url += "/";
    url += "models/" + model;

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    nr.setRawHeader("Accept", "image/png");
    if (!provider->apiKey.isEmpty())
        nr.setRawHeader("Authorization", ("Bearer " + provider->apiKey).toUtf8());

    QJsonObject body;
    body["inputs"] = prompt;
    m_reply = m_nam.post(nr, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &HuggingFaceClient::onImageFinished);
}

void HuggingFaceClient::onImageFinished()
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

    QString ctype = m_reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (ctype.startsWith("image/")) {
        emit imageReady(body, ctype);
    } else {
        const auto doc = QJsonDocument::fromJson(body).object();
        if (doc.contains("error")) emit error(doc.value("error").toString());
        else emit error("Unexpected response from image endpoint");
    }

    emit finished(QString());
    m_reply->deleteLater();
    m_reply = nullptr;
}

}
