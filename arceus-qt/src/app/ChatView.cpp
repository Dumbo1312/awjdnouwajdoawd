#include "ChatView.h"
#include "MessageWidget.h"
#include "core/Settings.h"
#include "core/Tools.h"
#include "net/ApiClient.h"
#include "net/ApiRouter.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QMimeDatabase>

using namespace arceus;

ChatView::ChatView(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName("ChatHeader");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(14, 8, 14, 8);
    m_title = new QLabel("New chat", header);
    m_title->setObjectName("TitleLabel");
    hl->addWidget(m_title, 1);

    m_presetCombo = new QComboBox(header);
    m_presetCombo->setObjectName("PresetButton");
    m_providerCombo = new QComboBox(header);
    m_providerCombo->setObjectName("ModelButton");
    m_modelCombo = new QComboBox(header);
    m_modelCombo->setObjectName("ModelButton");
    m_permissionCombo = new QComboBox(header);
    m_permissionCombo->setObjectName("PermButton");

    m_permissionCombo->addItem("Ask",           "ask");
    m_permissionCombo->addItem("Accept edits",  "accept_edits");
    m_permissionCombo->addItem("Bypass",        "bypass");
    m_permissionCombo->addItem("Read-only",     "read_only");

    hl->addWidget(m_presetCombo);
    hl->addWidget(m_providerCombo);
    hl->addWidget(m_modelCombo);
    hl->addWidget(m_permissionCombo);

    connect(m_presetCombo,    QOverload<int>::of(&QComboBox::activated), this, [this](int){ onPickPreset(); });
    connect(m_providerCombo,  QOverload<int>::of(&QComboBox::activated), this, [this](int){ onPickModel(); });
    connect(m_modelCombo,     QOverload<int>::of(&QComboBox::activated), this, [this](int){ onPickModel(); });
    connect(m_permissionCombo,QOverload<int>::of(&QComboBox::activated), this, [this](int){ onPickPermission(); });

    root->addWidget(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("ChatArea");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_messagesHost = new QWidget;
    m_messagesHost->setObjectName("ChatArea");
    m_messagesLay = new QVBoxLayout(m_messagesHost);
    m_messagesLay->setContentsMargins(60, 20, 60, 20);
    m_messagesLay->setSpacing(10);
    m_messagesLay->addStretch(1);
    m_scroll->setWidget(m_messagesHost);
    root->addWidget(m_scroll, 1);

    auto *composer = new QWidget(this);
    composer->setObjectName("Composer");
    auto *cv = new QVBoxLayout(composer);
    cv->setContentsMargins(6,6,6,6);
    cv->setSpacing(2);

    m_input = new QPlainTextEdit(composer);
    m_input->setPlaceholderText("Message Arceus… (Shift+Enter for newline)");
    m_input->setFixedHeight(80);
    cv->addWidget(m_input);

    auto *bar = new QWidget(composer);
    bar->setObjectName("ComposerBar");
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(4,0,4,0);
    m_attach = new QPushButton("📎 Attach", bar);
    m_attach->setObjectName("AttachButton");
    bl->addWidget(m_attach);
    m_attachmentStatus = new QLabel("", bar);
    m_attachmentStatus->setObjectName("MutedLabel");
    bl->addWidget(m_attachmentStatus, 1);
    m_send = new QPushButton("Send ▸", bar);
    m_send->setObjectName("SendButton");
    bl->addWidget(m_send);
    cv->addWidget(bar);

    root->addWidget(composer);

    connect(m_send, &QPushButton::clicked, this, &ChatView::onSend);
    connect(m_attach, &QPushButton::clicked, this, &ChatView::onAttach);

    class EnterFilter : public QObject {
    public:
        EnterFilter(ChatView *v) : QObject(v), v(v) {}
        bool eventFilter(QObject *o, QEvent *e) override {
            if (e->type() == QEvent::KeyPress) {
                auto *ke = static_cast<QKeyEvent*>(e);
                if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
                    !(ke->modifiers() & Qt::ShiftModifier)) {
                    v->onSend();
                    return true;
                }
            }
            return QObject::eventFilter(o, e);
        }
        ChatView *v;
    };
    m_input->installEventFilter(new EnterFilter(this));
}

void ChatView::setChat(Chat *chat)
{
    m_chat = chat;

    m_presetCombo->blockSignals(true);
    m_providerCombo->blockSignals(true);
    m_modelCombo->blockSignals(true);
    m_permissionCombo->blockSignals(true);

    m_presetCombo->clear();
    for (const auto &p : Settings::instance().presets())
        m_presetCombo->addItem(p.name, p.id);

    m_providerCombo->clear();
    for (const auto &p : Settings::instance().providers())
        if (p.enabled) m_providerCombo->addItem(p.name, p.id);

    if (chat) {
        int i = m_presetCombo->findData(chat->presetId);
        if (i < 0 && m_presetCombo->count()) {
            i = 0; chat->presetId = m_presetCombo->itemData(0).toString();
        }
        if (i >= 0) m_presetCombo->setCurrentIndex(i);

        i = m_providerCombo->findData(chat->providerId);
        if (i < 0 && m_providerCombo->count()) {
            i = 0; chat->providerId = m_providerCombo->itemData(0).toString();
        }
        if (i >= 0) m_providerCombo->setCurrentIndex(i);

        m_modelCombo->clear();
        if (const Provider *prov = Settings::instance().findProvider(chat->providerId)) {
            for (const auto &m : prov->models) m_modelCombo->addItem(m, m);
            if (chat->model.isEmpty() && !prov->models.isEmpty()) chat->model = prov->models.first();
            int j = m_modelCombo->findData(chat->model);
            if (j >= 0) m_modelCombo->setCurrentIndex(j);
        }

        int pi = m_permissionCombo->findData(chat->permissionMode.isEmpty() ? "ask" : chat->permissionMode);
        if (pi >= 0) m_permissionCombo->setCurrentIndex(pi);
    }

    m_presetCombo->blockSignals(false);
    m_providerCombo->blockSignals(false);
    m_modelCombo->blockSignals(false);
    m_permissionCombo->blockSignals(false);

    rebuildMessages();
    updateHeader();
}

void ChatView::updateHeader() {
    m_title->setText(m_chat && !m_chat->title.isEmpty() ? m_chat->title : "New chat");
}

void ChatView::rebuildMessages()
{
    while (QLayoutItem *it = m_messagesLay->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    m_messagesLay->addStretch(1);
    if (!m_chat) return;
    for (const auto &m : m_chat->messages) appendMessageWidget(m);
    QTimer::singleShot(0, this, [this]{ scrollToBottom(); });
}

MessageWidget *ChatView::appendMessageWidget(const Message &m)
{
    auto *w = new MessageWidget(m, m_messagesHost);
    m_messagesLay->insertWidget(m_messagesLay->count() - 1, w);
    return w;
}

void ChatView::scrollToBottom() {
    auto *b = m_scroll->verticalScrollBar();
    b->setValue(b->maximum());
}

void ChatView::setStreamingTarget(MessageWidget *w) { m_streamTarget = w; }

void ChatView::onPickPreset() {
    if (!m_chat) return;
    m_chat->presetId = m_presetCombo->currentData().toString();
    if (const Preset *p = Settings::instance().findPreset(m_chat->presetId)) {
        if (!p->defaultProviderId.isEmpty()) {
            m_chat->providerId = p->defaultProviderId;
            int i = m_providerCombo->findData(m_chat->providerId);
            if (i >= 0) m_providerCombo->setCurrentIndex(i);
        }
        if (!p->defaultModel.isEmpty()) m_chat->model = p->defaultModel;
    }
    setChat(m_chat);
    emit chatMutated();
}

void ChatView::onPickModel() {
    if (!m_chat) return;
    m_chat->providerId = m_providerCombo->currentData().toString();
    if (const Provider *prov = Settings::instance().findProvider(m_chat->providerId)) {
        m_modelCombo->blockSignals(true);
        m_modelCombo->clear();
        for (const auto &mod : prov->models) m_modelCombo->addItem(mod, mod);
        int j = m_modelCombo->findData(m_chat->model);
        if (j < 0 && !prov->models.isEmpty()) {
            m_chat->model = prov->models.first();
            j = 0;
        }
        if (j >= 0) m_modelCombo->setCurrentIndex(j);
        m_modelCombo->blockSignals(false);
    }
    m_chat->model = m_modelCombo->currentData().toString();
    emit chatMutated();
}

void ChatView::onPickPermission() {
    if (!m_chat) return;
    m_chat->permissionMode = m_permissionCombo->currentData().toString();
    emit chatMutated();
}

void ChatView::onAttach() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Attach files", QString(),
        "All files (*);;Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp);;Text (*.txt *.md *.json *.cpp *.h *.py *.js)");
    QMimeDatabase mdb;
    for (const QString &f : files) {
        QFile file(f);
        if (!file.open(QFile::ReadOnly)) continue;
        Attachment a;
        a.path = f;
        a.data = file.readAll();
        a.mime = mdb.mimeTypeForFile(f).name();
        m_pendingAttachments.append(a);
    }
    if (m_pendingAttachments.isEmpty()) m_attachmentStatus->setText("");
    else m_attachmentStatus->setText(QString("%1 attached").arg(m_pendingAttachments.size()));
}

void ChatView::onSend()
{
    if (!m_chat) return;
    QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() && m_pendingAttachments.isEmpty()) return;

    for (const auto &a : m_pendingAttachments) {
        if (a.mime.startsWith("text/") || a.mime.contains("json") || a.mime.contains("xml") ||
            a.path.endsWith(".cpp") || a.path.endsWith(".h") || a.path.endsWith(".md") ||
            a.path.endsWith(".py") || a.path.endsWith(".js") || a.path.endsWith(".ts")) {
            text += QString("\n\n--- Attached file: %1 ---\n").arg(QFileInfo(a.path).fileName());
            text += QString::fromUtf8(a.data);
            text += "\n--- End of file ---\n";
        } else {
            text += QString("\n[Attached binary: %1 (%2 bytes)]").arg(QFileInfo(a.path).fileName()).arg(a.data.size());
        }
    }

    Message um;
    um.role = Role::User;
    um.content = text;
    um.attachments = m_pendingAttachments;
    um.createdAt = QDateTime::currentDateTime();
    m_chat->messages.append(um);
    appendMessageWidget(um);

    m_input->clear();
    m_pendingAttachments.clear();
    m_attachmentStatus->setText("");

    if (m_chat->title.isEmpty() || m_chat->title == "New chat") {
        m_chat->title = text.left(48).replace('\n', ' ');
        updateHeader();
    }

    emit chatMutated();
    runTurn();
}

QString ChatView::workspaceForChat() const {
    return Settings::instance().workspaceDir();
}

void ChatView::runTurn()
{
    if (!m_chat) return;
    const Provider *prov = Settings::instance().findProvider(m_chat->providerId);
    const Preset *pre = Settings::instance().findPreset(m_chat->presetId);
    if (!prov) { QMessageBox::warning(this, "Arceus", "No provider selected."); return; }
    if (prov->apiKey.isEmpty() && !prov->baseUrl.contains("localhost") && !prov->baseUrl.contains("127.0.0.1")) {
        QMessageBox::warning(this, "Arceus",
            QString("No API key set for %1. Open Settings to add one.").arg(prov->name));
        return;
    }

    if (pre && pre->enableImages) {
        Message holder;
        holder.role = Role::Assistant;
        holder.content = "Generating image…";
        m_chat->messages.append(holder);
        auto *w = appendMessageWidget(m_chat->messages.last());

        auto *client = ApiRouter::make(*prov, this);
        connect(client, &ApiClient::imageReady, this, [this, w](const QByteArray &b, const QString &mime){
            if (!m_chat || m_chat->messages.isEmpty()) return;
            auto &msg = m_chat->messages.last();
            msg.content = "Image generated.";
            w->appendText("");
            w->setImage(b, mime);
            emit chatMutated();
        });
        connect(client, &ApiClient::error, this, [this, w](const QString &err){
            w->appendText("\n*Error:* " + err);
            emit chatMutated();
        });
        connect(client, &ApiClient::finished, client, &QObject::deleteLater);

        const auto &last = m_chat->messages[m_chat->messages.size()-2];
        client->generateImage(prov, m_chat->model, last.content);
        return;
    }

    Message holder;
    holder.role = Role::Assistant;
    holder.content = "";
    m_chat->messages.append(holder);
    auto *w = appendMessageWidget(m_chat->messages.last());
    setStreamingTarget(w);

    if (m_client) m_client->deleteLater();
    m_client = ApiRouter::make(*prov, this);

    connect(m_client, &ApiClient::deltaText, this, [this](const QString &chunk){
        if (!m_chat || m_chat->messages.isEmpty()) return;
        m_chat->messages.last().content += chunk;
        if (m_streamTarget) m_streamTarget->appendText(chunk);
        scrollToBottom();
    });

    connect(m_client, &ApiClient::toolCall, this, [this](const ToolCall &call){
        runToolCall(call);
    });

    connect(m_client, &ApiClient::error, this, [this](const QString &err){
        if (!m_chat || m_chat->messages.isEmpty()) return;
        auto &last = m_chat->messages.last();
        if (last.role == Role::Assistant) {
            last.content += "\n\n*Error:* " + err;
            if (m_streamTarget) m_streamTarget->appendText("\n\n*Error:* " + err);
        }
        emit chatMutated();
    });

    connect(m_client, &ApiClient::finished, this, [this](const QString &){
        emit chatMutated();
    });

    ChatRequest req;
    req.provider = prov;
    req.preset = pre;
    req.model = m_chat->model;
    req.enableTools = pre && pre->enableTools;
    req.stream = (prov->kind == ProviderKind::OpenAICompat || prov->kind == ProviderKind::Anthropic);
    for (const auto &m : m_chat->messages) {
        if (&m == &m_chat->messages.last()) break;
        req.messages.append(m);
    }
    m_client->sendChat(req);
}

void ChatView::runToolCall(const ToolCall &call)
{
    if (!m_chat) return;

    auto &assistantMsg = m_chat->messages.last();
    assistantMsg.toolCallId = call.id;
    assistantMsg.toolName = call.name;
    assistantMsg.toolArgs = call.arguments;

    PermissionMode mode = permissionModeFromString(m_chat->permissionMode);
    auto asker = [this, &call](const ToolCall &tc, QString *reason) -> bool {
        Q_UNUSED(reason);
        QString pretty = QJsonDocument(tc.arguments).toJson(QJsonDocument::Indented);
        int ret = QMessageBox::question(this, "Arceus permission",
            QString("Allow tool `%1`?\n\n%2").arg(tc.name, QString::fromUtf8(pretty)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return ret == QMessageBox::Yes;
    };

    ToolRunner runner(workspaceForChat(), mode, asker);
    ToolResult r = runner.run(call);

    Message toolMsg;
    toolMsg.role = Role::Tool;
    toolMsg.toolCallId = call.id;
    toolMsg.toolName = call.name;
    toolMsg.content = r.content;
    toolMsg.createdAt = QDateTime::currentDateTime();
    m_chat->messages.append(toolMsg);
    appendMessageWidget(toolMsg);

    Message holder;
    holder.role = Role::Assistant;
    holder.content = "";
    m_chat->messages.append(holder);
    auto *w = appendMessageWidget(holder);
    setStreamingTarget(w);

    const Provider *prov = Settings::instance().findProvider(m_chat->providerId);
    const Preset *pre = Settings::instance().findPreset(m_chat->presetId);
    if (!prov) return;

    if (m_client) m_client->deleteLater();
    m_client = ApiRouter::make(*prov, this);
    connect(m_client, &ApiClient::deltaText, this, [this](const QString &chunk){
        if (!m_chat || m_chat->messages.isEmpty()) return;
        m_chat->messages.last().content += chunk;
        if (m_streamTarget) m_streamTarget->appendText(chunk);
        scrollToBottom();
    });
    connect(m_client, &ApiClient::toolCall, this, [this](const ToolCall &c){ runToolCall(c); });
    connect(m_client, &ApiClient::error, this, [this](const QString &err){
        if (!m_chat || m_chat->messages.isEmpty()) return;
        auto &last = m_chat->messages.last();
        last.content += "\n\n*Error:* " + err;
        if (m_streamTarget) m_streamTarget->appendText("\n\n*Error:* " + err);
        emit chatMutated();
    });
    connect(m_client, &ApiClient::finished, this, [this](const QString &){ emit chatMutated(); });

    ChatRequest req;
    req.provider = prov;
    req.preset = pre;
    req.model = m_chat->model;
    req.enableTools = pre && pre->enableTools;
    req.stream = (prov->kind == ProviderKind::OpenAICompat || prov->kind == ProviderKind::Anthropic);
    for (const auto &m : m_chat->messages) {
        if (&m == &m_chat->messages.last()) break;
        req.messages.append(m);
    }
    m_client->sendChat(req);
}
