#include "Settings.h"
#include "Defaults.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace arceus {

Settings &Settings::instance() {
    static Settings s;
    return s;
}

QString Settings::configDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString Settings::configFile() {
    return configDir() + "/arceus.json";
}

QString Settings::newId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

const Preset *Settings::findPreset(const QString &id) const {
    for (const auto &p : m_presets) if (p.id == id) return &p;
    return nullptr;
}
const Provider *Settings::findProvider(const QString &id) const {
    for (const auto &p : m_providers) if (p.id == id) return &p;
    return nullptr;
}
Provider *Settings::findProviderMut(const QString &id) {
    for (auto &p : m_providers) if (p.id == id) return &p;
    return nullptr;
}

void Settings::load() {
    QDir().mkpath(configDir());
    QFile f(configFile());

    if (!f.exists()) {
        m_presets = Defaults::presets(this);
        m_providers = Defaults::providers();
        m_defaultProviderId = m_providers.isEmpty() ? QString() : m_providers.first().id;
        m_defaultPresetId = m_presets.isEmpty() ? QString() : m_presets.first().id;
        m_defaultPermissionMode = "ask";
        m_workspaceDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/ArceusAI";
        QDir().mkpath(m_workspaceDir);
        save();
        return;
    }

    if (!f.open(QFile::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto o = doc.object();

    m_defaultProviderId = o.value("default_provider_id").toString();
    m_defaultPresetId = o.value("default_preset_id").toString();
    m_defaultPermissionMode = o.value("default_permission_mode").toString("ask");
    m_workspaceDir = o.value("workspace_dir").toString(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/ArceusAI");
    QDir().mkpath(m_workspaceDir);

    m_providers.clear();
    for (const auto &v : o.value("providers").toArray())
        m_providers.append(Provider::fromJson(v.toObject()));

    m_presets.clear();
    for (const auto &v : o.value("presets").toArray())
        m_presets.append(Preset::fromJson(v.toObject()));

    if (m_presets.isEmpty()) m_presets = Defaults::presets(this);
    if (m_providers.isEmpty()) m_providers = Defaults::providers();

    m_chats.clear();
    for (const auto &v : o.value("chats").toArray())
        m_chats.append(Chat::fromJson(v.toObject()));
}

void Settings::save() {
    QDir().mkpath(configDir());
    QJsonObject o;
    o["default_provider_id"] = m_defaultProviderId;
    o["default_preset_id"] = m_defaultPresetId;
    o["default_permission_mode"] = m_defaultPermissionMode;
    o["workspace_dir"] = m_workspaceDir;

    QJsonArray arrProv, arrPre, arrChat;
    for (const auto &p : m_providers) arrProv.append(p.toJson());
    for (const auto &p : m_presets) arrPre.append(p.toJson());
    for (const auto &c : m_chats) arrChat.append(c.toJson());
    o["providers"] = arrProv;
    o["presets"] = arrPre;
    o["chats"] = arrChat;

    QFile f(configFile());
    if (f.open(QFile::WriteOnly | QFile::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
}

}
