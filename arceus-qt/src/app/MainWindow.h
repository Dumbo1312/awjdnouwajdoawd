#pragma once
#include <QMainWindow>
#include <QListWidget>
#include "core/Chat.h"

class ChatView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void onNewChat();
    void onDeleteChat();
    void onRenameChat();
    void onPickChat(int row);
    void onOpenSettings();
    void onChatMutated();

private:
    void refreshChatList();
    int findRowForChat(const QString &id) const;

    QListWidget *m_list = nullptr;
    ChatView *m_view = nullptr;
    QString m_activeChatId;
};
