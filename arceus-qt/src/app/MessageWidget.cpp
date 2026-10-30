#include "MessageWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QBuffer>
#include <QTextDocument>
#include <QFontDatabase>

using namespace arceus;

static QString escapeHtml(const QString &s) {
    QString r = s;
    r.replace("&", "&amp;")
     .replace("<", "&lt;")
     .replace(">", "&gt;");
    return r;
}

static QString renderMarkdown(const QString &md)
{
    QTextDocument d;
    d.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    return d.toHtml();
}

MessageWidget::MessageWidget(const Message &msg, QWidget *parent)
    : QFrame(parent), m_msg(msg)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(4);

    m_role = new QLabel(this);
    switch (msg.role) {
    case Role::User:      m_role->setText("You"); m_role->setObjectName("RoleLabelUser"); break;
    case Role::Assistant: m_role->setText("Arceus"); m_role->setObjectName("RoleLabel"); break;
    case Role::System:    m_role->setText("System"); m_role->setObjectName("RoleLabel"); break;
    case Role::Tool:      m_role->setText("Tool · " + msg.toolName); m_role->setObjectName("RoleLabelTool"); break;
    }
    lay->addWidget(m_role);

    m_body = new QTextBrowser(this);
    m_body->setOpenExternalLinks(true);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setStyleSheet("background: transparent; border: none;");
    m_body->document()->setDefaultStyleSheet(
        "a { color: #FFD54A; } "
        "code { background: #0E0E0F; color: #FFD54A; padding: 1px 4px; border-radius: 4px; font-family: Consolas, monospace; } "
        "pre { background: #0A0A0B; padding: 10px; border-radius: 8px; }"
    );
    m_body->setMinimumHeight(10);
    lay->addWidget(m_body);

    m_image = new QLabel(this);
    m_image->setVisible(false);
    lay->addWidget(m_image);

    if (msg.role == Role::User) setObjectName("MessageBubbleUser");
    else if (msg.role == Role::Tool) setObjectName("MessageBubbleTool");
    else setObjectName("MessageBubble");

    m_buffer = msg.content;
    refresh();
}

void MessageWidget::appendText(const QString &chunk) {
    m_buffer += chunk;
    m_msg.content = m_buffer;
    refresh();
}

void MessageWidget::setImage(const QByteArray &bytes, const QString &mime) {
    Q_UNUSED(mime);
    QPixmap p;
    if (p.loadFromData(bytes)) {
        if (p.width() > 720) p = p.scaledToWidth(720, Qt::SmoothTransformation);
        m_image->setPixmap(p);
        m_image->setVisible(true);
    }
}

void MessageWidget::refresh() {
    if (m_msg.role == Role::Tool) {
        m_body->setPlainText(m_buffer);
    } else {
        m_body->setHtml(renderMarkdown(m_buffer));
    }
    m_body->document()->setTextWidth(m_body->viewport()->width());
    int h = int(m_body->document()->size().height()) + 10;
    m_body->setFixedHeight(qMax(24, h));
}
