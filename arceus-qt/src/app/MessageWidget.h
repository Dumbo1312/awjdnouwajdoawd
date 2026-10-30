#pragma once
#include <QFrame>
#include <QLabel>
#include <QTextBrowser>
#include "core/Chat.h"

class MessageWidget : public QFrame {
    Q_OBJECT
public:
    explicit MessageWidget(const arceus::Message &msg, QWidget *parent = nullptr);
    void appendText(const QString &chunk);
    void setImage(const QByteArray &bytes, const QString &mime);
    const arceus::Message &message() const { return m_msg; }

private:
    arceus::Message m_msg;
    QLabel *m_role = nullptr;
    QTextBrowser *m_body = nullptr;
    QLabel *m_image = nullptr;
    QString m_buffer;
    void refresh();
};
