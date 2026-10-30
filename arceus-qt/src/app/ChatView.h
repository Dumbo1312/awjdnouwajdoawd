#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include "core/Chat.h"
#include "core/Preset.h"
#include "core/Provider.h"
#include "core/Tools.h"

class MessageWidget;
namespace arceus { class ApiClient; }

class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    void setChat(arceus::Chat *chat);
    arceus::Chat *chat() const { return m_chat; }

signals:
    void chatMutated();

private slots:
    void onSend();
    void onAttach();
    void onPickPreset();
    void onPickModel();
    void onPickPermission();

private:
    void rebuildMessages();
    MessageWidget *appendMessageWidget(const arceus::Message &m);
    void setStreamingTarget(MessageWidget *w);
    void runTurn();
    void runToolCall(const arceus::ToolCall &call);
    void updateHeader();
    void scrollToBottom();
    QString workspaceForChat() const;

    arceus::Chat *m_chat = nullptr;
    arceus::ApiClient *m_client = nullptr;
    MessageWidget *m_streamTarget = nullptr;

    QLabel *m_title = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_permissionCombo = nullptr;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesHost = nullptr;
    QVBoxLayout *m_messagesLay = nullptr;

    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_attach = nullptr;
    QVector<arceus::Attachment> m_pendingAttachments;
    QLabel *m_attachmentStatus = nullptr;
};
