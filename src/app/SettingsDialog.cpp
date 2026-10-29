#include "SettingsDialog.h"
#include "core/Settings.h"
#include "core/Defaults.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>

using namespace arceus;

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Arceus — Settings");
    resize(960, 640);

    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    auto *provTab = new QWidget;
    auto *preTab = new QWidget;
    auto *genTab = new QWidget;

    tabs->addTab(provTab, "Providers & API keys");
    tabs->addTab(preTab, "Presets");
    tabs->addTab(genTab, "General");

    root->addWidget(tabs, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);

    {
        auto *lay = new QHBoxLayout(provTab);
        auto *left = new QVBoxLayout;
        m_providerList = new QListWidget;
        left->addWidget(m_providerList, 1);
        auto *lbtn = new QHBoxLayout;
        auto *add = new QPushButton("+ Add");
        auto *rem = new QPushButton("− Remove");
        rem->setObjectName("DangerButton");
        lbtn->addWidget(add); lbtn->addWidget(rem);
        left->addLayout(lbtn);
        lay->addLayout(left, 1);

        auto *right = new QFormLayout;
        m_pName   = new QLineEdit;
        m_pKind   = new QComboBox;
        m_pKind->addItem("OpenAI-compatible", "openai_compat");
        m_pKind->addItem("Anthropic",          "anthropic");
        m_pKind->addItem("Google Gemini",      "gemini");
        m_pKind->addItem("Hugging Face",       "huggingface");
        m_pBase   = new QLineEdit;
        m_pKey    = new QLineEdit;
        m_pKey->setEchoMode(QLineEdit::Password);
        m_pModels = new QPlainTextEdit;
        m_pModels->setPlaceholderText("One model per line");
        m_pModels->setMinimumHeight(120);
        m_pImages = new QCheckBox("Supports image generation");
        m_pEnabled = new QCheckBox("Enabled");
        m_pBuiltin = new QLabel("");
        m_pBuiltin->setObjectName("MutedLabel");

        right->addRow("Name", m_pName);
        right->addRow("Kind", m_pKind);
        right->addRow("Base URL", m_pBase);
        right->addRow("API key", m_pKey);
        right->addRow("Models", m_pModels);
        right->addRow("", m_pImages);
        right->addRow("", m_pEnabled);
        right->addRow("", m_pBuiltin);
        lay->addLayout(right, 2);

        connect(m_providerList, &QListWidget::currentRowChanged, this, &SettingsDialog::onProviderSelected);
        connect(add, &QPushButton::clicked, this, &SettingsDialog::onAddProvider);
        connect(rem, &QPushButton::clicked, this, &SettingsDialog::onRemoveProvider);
    }

    {
        auto *lay = new QHBoxLayout(preTab);
        auto *left = new QVBoxLayout;
        m_presetList = new QListWidget;
        left->addWidget(m_presetList, 1);
        auto *lbtn = new QHBoxLayout;
        auto *add = new QPushButton("+ Add");
        auto *rem = new QPushButton("− Remove");
        rem->setObjectName("DangerButton");
        lbtn->addWidget(add); lbtn->addWidget(rem);
        left->addLayout(lbtn);
        lay->addLayout(left, 1);

        auto *right = new QFormLayout;
        m_prName = new QLineEdit;
        m_prProv = new QComboBox;
        m_prModel = new QComboBox;
        m_prModel->setEditable(true);
        m_prSys = new QPlainTextEdit;
        m_prSys->setMinimumHeight(200);
        m_prTools = new QCheckBox("Enable tools (agentic)");
        m_prImages = new QCheckBox("Image generation preset");
        m_prTemp = new QLineEdit;
        m_prMax = new QLineEdit;

        right->addRow("Name", m_prName);
        right->addRow("Default provider", m_prProv);
        right->addRow("Default model", m_prModel);
        right->addRow("System prompt", m_prSys);
        right->addRow("", m_prTools);
        right->addRow("", m_prImages);
        right->addRow("Temperature", m_prTemp);
        right->addRow("Max tokens", m_prMax);
        lay->addLayout(right, 2);

        connect(m_presetList, &QListWidget::currentRowChanged, this, &SettingsDialog::onPresetSelected);
        connect(add, &QPushButton::clicked, this, &SettingsDialog::onAddPreset);
        connect(rem, &QPushButton::clicked, this, &SettingsDialog::onRemovePreset);
        connect(m_prProv, QOverload<int>::of(&QComboBox::activated), this, [this](int){
            m_prModel->clear();
            const QString pid = m_prProv->currentData().toString();
            if (const Provider *p = Settings::instance().findProvider(pid))
                for (const auto &m : p->models) m_prModel->addItem(m);
        });
    }

    {
        auto *lay = new QFormLayout(genTab);
        m_defaultProvider = new QComboBox;
        m_defaultPreset = new QComboBox;
        m_defaultPerm = new QComboBox;
        m_defaultPerm->addItem("Ask",          "ask");
        m_defaultPerm->addItem("Accept edits", "accept_edits");
        m_defaultPerm->addItem("Bypass",       "bypass");
        m_defaultPerm->addItem("Read-only",    "read_only");
        m_workspace = new QLineEdit(Settings::instance().workspaceDir());
        auto *pickWs = new QPushButton("Browse…");
        connect(pickWs, &QPushButton::clicked, this, [this]{
            QString d = QFileDialog::getExistingDirectory(this, "Agentic workspace", m_workspace->text());
            if (!d.isEmpty()) m_workspace->setText(d);
        });
        auto *wsBox = new QHBoxLayout;
        wsBox->addWidget(m_workspace, 1);
        wsBox->addWidget(pickWs);

        for (const auto &p : Settings::instance().providers())
            m_defaultProvider->addItem(p.name, p.id);
        for (const auto &p : Settings::instance().presets())
            m_defaultPreset->addItem(p.name, p.id);

        int i = m_defaultProvider->findData(Settings::instance().defaultProviderId());
        if (i >= 0) m_defaultProvider->setCurrentIndex(i);
        i = m_defaultPreset->findData(Settings::instance().defaultPresetId());
        if (i >= 0) m_defaultPreset->setCurrentIndex(i);
        i = m_defaultPerm->findData(Settings::instance().defaultPermissionMode());
        if (i >= 0) m_defaultPerm->setCurrentIndex(i);

        lay->addRow("Default provider", m_defaultProvider);
        lay->addRow("Default preset", m_defaultPreset);
        lay->addRow("Default permission mode", m_defaultPerm);
        lay->addRow("Agentic workspace", wsBox);

        auto *info = new QLabel(
            QString("Config file: %1").arg(Settings::configFile()));
        info->setObjectName("MutedLabel");
        info->setWordWrap(true);
        lay->addRow(info);
    }

    reloadProviders();
    reloadPresets();
    if (m_providerList->count()) m_providerList->setCurrentRow(0);
    if (m_presetList->count()) m_presetList->setCurrentRow(0);
}

void SettingsDialog::reloadProviders() {
    m_providerList->blockSignals(true);
    m_providerList->clear();
    for (const auto &p : Settings::instance().providers()) {
        auto *it = new QListWidgetItem(p.name + (p.builtin ? "  (builtin)" : ""));
        it->setData(Qt::UserRole, p.id);
        m_providerList->addItem(it);
    }
    m_providerList->blockSignals(false);
}

void SettingsDialog::reloadPresets() {
    m_presetList->blockSignals(true);
    m_presetList->clear();
    for (const auto &p : Settings::instance().presets()) {
        auto *it = new QListWidgetItem(p.name);
        it->setData(Qt::UserRole, p.id);
        m_presetList->addItem(it);
    }
    m_presetList->blockSignals(false);

    m_prProv->blockSignals(true);
    m_prProv->clear();
    for (const auto &p : Settings::instance().providers())
        m_prProv->addItem(p.name, p.id);
    m_prProv->blockSignals(false);
}

void SettingsDialog::onProviderSelected(int row) {
    if (m_currentProvider >= 0) saveCurrentProvider();
    m_currentProvider = row;
    if (row < 0 || row >= Settings::instance().providers().size()) return;
    const auto &p = Settings::instance().providers().at(row);
    m_pName->setText(p.name);
    int ki = m_pKind->findData(providerKindToString(p.kind));
    if (ki >= 0) m_pKind->setCurrentIndex(ki);
    m_pBase->setText(p.baseUrl);
    m_pKey->setText(p.apiKey);
    m_pModels->setPlainText(p.models.join("\n"));
    m_pImages->setChecked(p.supportsImages);
    m_pEnabled->setChecked(p.enabled);
    m_pBuiltin->setText(p.builtin ? "Built-in provider — editable, but reset by Arceus if removed." : "");
}

void SettingsDialog::saveCurrentProvider() {
    if (m_currentProvider < 0 || m_currentProvider >= Settings::instance().providers().size()) return;
    auto &p = Settings::instance().providers()[m_currentProvider];
    p.name = m_pName->text();
    p.kind = providerKindFromString(m_pKind->currentData().toString());
    p.baseUrl = m_pBase->text();
    p.apiKey = m_pKey->text();
    p.supportsImages = m_pImages->isChecked();
    p.enabled = m_pEnabled->isChecked();
    p.models.clear();
    for (const QString &m : m_pModels->toPlainText().split('\n', Qt::SkipEmptyParts))
        p.models.append(m.trimmed());
}

void SettingsDialog::onAddProvider() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New provider", "Name:", QLineEdit::Normal, "My provider", &ok);
    if (!ok || name.isEmpty()) return;
    Provider p;
    p.id = Settings::instance().newId();
    p.name = name;
    p.kind = ProviderKind::OpenAICompat;
    p.enabled = true;
    p.builtin = false;
    Settings::instance().providers().append(p);
    reloadProviders();
    m_providerList->setCurrentRow(m_providerList->count()-1);
}

void SettingsDialog::onRemoveProvider() {
    if (m_currentProvider < 0 || m_currentProvider >= Settings::instance().providers().size()) return;
    if (Settings::instance().providers()[m_currentProvider].builtin) {
        if (QMessageBox::question(this, "Remove", "This is a built-in provider. Remove anyway?")
            != QMessageBox::Yes) return;
    }
    Settings::instance().providers().remove(m_currentProvider);
    m_currentProvider = -1;
    reloadProviders();
    if (m_providerList->count()) m_providerList->setCurrentRow(0);
}

void SettingsDialog::onPresetSelected(int row) {
    if (m_currentPreset >= 0) saveCurrentPreset();
    m_currentPreset = row;
    if (row < 0 || row >= Settings::instance().presets().size()) return;
    const auto &p = Settings::instance().presets().at(row);
    m_prName->setText(p.name);
    int i = m_prProv->findData(p.defaultProviderId);
    if (i >= 0) m_prProv->setCurrentIndex(i);
    m_prModel->clear();
    if (const Provider *prov = Settings::instance().findProvider(p.defaultProviderId))
        for (const auto &m : prov->models) m_prModel->addItem(m);
    m_prModel->setEditText(p.defaultModel);
    m_prSys->setPlainText(p.systemPrompt);
    m_prTools->setChecked(p.enableTools);
    m_prImages->setChecked(p.enableImages);
    m_prTemp->setText(QString::number(p.temperature));
    m_prMax->setText(QString::number(p.maxTokens));
}

void SettingsDialog::saveCurrentPreset() {
    if (m_currentPreset < 0 || m_currentPreset >= Settings::instance().presets().size()) return;
    auto &p = Settings::instance().presets()[m_currentPreset];
    p.name = m_prName->text();
    p.defaultProviderId = m_prProv->currentData().toString();
    p.defaultModel = m_prModel->currentText();
    p.systemPrompt = m_prSys->toPlainText();
    p.enableTools = m_prTools->isChecked();
    p.enableImages = m_prImages->isChecked();
    p.temperature = m_prTemp->text().toDouble();
    p.maxTokens = m_prMax->text().toInt();
}

void SettingsDialog::onAddPreset() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New preset", "Name:", QLineEdit::Normal, "Custom", &ok);
    if (!ok || name.isEmpty()) return;
    Preset p;
    p.id = Settings::instance().newId();
    p.name = name;
    if (!Settings::instance().providers().isEmpty())
        p.defaultProviderId = Settings::instance().providers().first().id;
    p.systemPrompt = "You are a helpful assistant.";
    Settings::instance().presets().append(p);
    reloadPresets();
    m_presetList->setCurrentRow(m_presetList->count()-1);
}

void SettingsDialog::onRemovePreset() {
    if (m_currentPreset < 0 || m_currentPreset >= Settings::instance().presets().size()) return;
    Settings::instance().presets().remove(m_currentPreset);
    m_currentPreset = -1;
    reloadPresets();
    if (m_presetList->count()) m_presetList->setCurrentRow(0);
}

void SettingsDialog::accept() {
    saveCurrentProvider();
    saveCurrentPreset();
    Settings::instance().setDefaultProviderId(m_defaultProvider->currentData().toString());
    Settings::instance().setDefaultPresetId(m_defaultPreset->currentData().toString());
    Settings::instance().setDefaultPermissionMode(m_defaultPerm->currentData().toString());
    Settings::instance().setWorkspaceDir(m_workspace->text());
    QDialog::accept();
}
