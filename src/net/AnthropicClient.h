#pragma once
#include "ApiClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace arceus {

class AnthropicClient : public ApiClient {
    Q_OBJECT
public:
    explicit AnthropicClient(QObject *parent = nullptr);
    void sendChat(const ChatRequest &req) override;
    void cancel() override;

private slots:
    void onReadyRead();
    void onFinished();

private:
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_accumulated;
    QJsonObject m_pendingToolCall;
    QString m_pendingToolInputJson;
};

}
