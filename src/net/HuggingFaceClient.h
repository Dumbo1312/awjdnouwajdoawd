#pragma once
#include "ApiClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace arceus {

class HuggingFaceClient : public ApiClient {
    Q_OBJECT
public:
    explicit HuggingFaceClient(QObject *parent = nullptr);
    void sendChat(const ChatRequest &req) override;
    void generateImage(const Provider *provider, const QString &model, const QString &prompt) override;
    void cancel() override;

private slots:
    void onChatFinished();
    void onImageFinished();

private:
    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
};

}
