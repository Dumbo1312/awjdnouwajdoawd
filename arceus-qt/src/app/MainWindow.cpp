#include "MainWindow.h"
#include "ChatView.h"
#include "SettingsDialog.h"
#include "core/Settings.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

using namespace arceus;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Arceus AI");
    resize(1240, 820);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    auto *sidebar = new QWidget(central);
    sidebar->setObjectName("Sidebar");
    sidebar->setMinimumWidth(260);
    sidebar->setMaximumWidth(320);
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(0,0,0,0);
    sv->setSpacing(0);

    auto *brand = new QLabel("⚡ ARCEUS AI", sidebar);
    brand->setObjectName("SidebarHeader");
    sv->addWidget(brand);

    auto *newBtn = new QPushButton("+  New chat", sidebar);
    newBtn->setObjectName("NewChatButton");
    sv->addWidget(newBtn);

    m_list = new QListWidget(sidebar);
    m_list->setObjectName("ChatList");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    sv->addWidget(m_list, 1);

    auto *footer = new QWidget(sidebar);
    footer->setObjectName("SidebarFooter");
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(10,8,10,8);
    auto *settingsBtn = new QPushButton("⚙  Settings", footer);
    settingsBtn->setObjectName("GhostButton");
    fl->addWidget(settingsBtn, 1);
    sv->addWidget(footer);

    root->addWidget(sidebar);

    m_view = new ChatView(central);
    root->addWidget(m_view, 1);

    connect(newBtn, &QPushButton::clicked, this, &MainWindow::onNewChat);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::onPickChat);
    connect(m_view, &ChatView::chatMutated, this, &MainWindow::onChatMutated);

    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p){
        QListWidgetItem *item = m_list->itemAt(p);
        if (!item) return;
        QMenu menu(this);
        QAction *rename = menu.addAction("Rename");
        QAction *del = menu.addAction("Delete");
        QAction *chosen = menu.exec(m_list->mapToGlobal(p));
        if (chosen == rename) { m_activeChatId = item->data(Qt::UserRole).toString(); onRenameChat(); }
        if (chosen == del)    { m_activeChatId = item->data(Qt::UserRole).toString(); onDeleteChat(); }
    });

    auto *menu = menuBar()->addMenu("&File");
    auto addAct = [this, menu](const QString &text, const QString &sc, auto slot){
        QAction *a = menu->addAction(text);
        if (!sc.isEmpty()) a->setShortcut(QKeySequence(sc));
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    addAct("New chat", "Ctrl+N", &MainWindow::onNewChat);
    addAct("Delete chat", "Ctrl+Delete", &MainWindow::onDeleteChat);
    addAct("Settings…", "Ctrl+,", &MainWindow::onOpenSettings);
    menu->addSeparator();
    QAction *qa = menu->addAction("Quit");
    qa->setShortcut(QKeySequence("Ctrl+Q"));
    connect(qa, &QAction::triggered, qApp, &QApplication::quit);

    refreshChatList();

    if (Settings::instance().chats().isEmpty()) {
        onNewChat();
    } else {
        m_list->setCurrentRow(0);
    }

    statusBar()->showMessage(QString("Config: %1").arg(Settings::configFile()));
}

void MainWindow::refreshChatList() {
    m_list->blockSignals(true);
    m_list->clear();
    for (const auto &c : Settings::instance().chats()) {
        auto *it = new QListWidgetItem(c.title.isEmpty() ? "New chat" : c.title);
        it->setData(Qt::UserRole, c.id);
        m_list->addItem(it);
    }
    m_list->blockSignals(false);
}

int MainWindow::findRowForChat(const QString &id) const {
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == id) return i;
    return -1;
}

void MainWindow::onNewChat() {
    Chat c;
    c.id = Settings::instance().newId();
    c.title = "New chat";
    c.updatedAt = QDateTime::currentDateTime();
    c.permissionMode = Settings::instance().defaultPermissionMode();
    c.presetId = Settings::instance().defaultPresetId();
    if (const Preset *p = Settings::instance().findPreset(c.presetId)) {
        c.providerId = p->defaultProviderId;
        c.model = p->defaultModel;
    }
    if (c.providerId.isEmpty()) c.providerId = Settings::instance().defaultProviderId();
    Settings::instance().chats().prepend(c);
    Settings::instance().save();

    refreshChatList();
    m_list->setCurrentRow(0);
}

void MainWindow::onDeleteChat() {
    QString id = m_activeChatId;
    if (id.isEmpty() && m_list->currentItem())
        id = m_list->currentItem()->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    if (QMessageBox::question(this, "Delete chat", "Delete this chat permanently?")
        != QMessageBox::Yes) return;

    auto &chats = Settings::instance().chats();
    for (int i = 0; i < chats.size(); ++i) {
        if (chats[i].id == id) { chats.remove(i); break; }
    }
    Settings::instance().save();
    refreshChatList();
    if (!chats.isEmpty()) m_list->setCurrentRow(0);
    else onNewChat();
}

void MainWindow::onRenameChat() {
    QString id = m_activeChatId;
    if (id.isEmpty() && m_list->currentItem())
        id = m_list->currentItem()->data(Qt::UserRole).toString();
    for (auto &c : Settings::instance().chats()) {
        if (c.id != id) continue;
        bool ok = false;
        QString t = QInputDialog::getText(this, "Rename chat", "Title:", QLineEdit::Normal, c.title, &ok);
        if (ok) { c.title = t; Settings::instance().save(); refreshChatList(); }
        return;
    }
}

void MainWindow::onPickChat(int row) {
    if (row < 0 || row >= m_list->count()) { m_view->setChat(nullptr); return; }
    QString id = m_list->item(row)->data(Qt::UserRole).toString();
    for (auto &c : Settings::instance().chats()) {
        if (c.id == id) {
            m_activeChatId = id;
            m_view->setChat(&c);
            return;
        }
    }
}

void MainWindow::onChatMutated() {
    if (Chat *c = m_view->chat()) {
        c->updatedAt = QDateTime::currentDateTime();
        int r = findRowForChat(c->id);
        if (r >= 0) m_list->item(r)->setText(c->title.isEmpty() ? "New chat" : c->title);
    }
    Settings::instance().save();
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Settings::instance().save();
        if (Chat *c = m_view->chat()) m_view->setChat(c);
    }
}
