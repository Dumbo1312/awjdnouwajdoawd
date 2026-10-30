#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include "Chat.h"
#include "Preset.h"
#include "Provider.h"

namespace arceus {

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    void load();
    void save();

    QVector<Chat> &chats() { return m_chats; }
    QVector<Preset> &presets() { return m_presets; }
    QVector<Provider> &providers() { return m_providers; }

    const Preset *findPreset(const QString &id) const;
    const Provider *findProvider(const QString &id) const;
    Provider *findProviderMut(const QString &id);

    QString defaultProviderId() const { return m_defaultProviderId; }
    QString defaultPresetId() const { return m_defaultPresetId; }
    QString defaultPermissionMode() const { return m_defaultPermissionMode; }
    void setDefaultProviderId(const QString &id) { m_defaultProviderId = id; }
    void setDefaultPresetId(const QString &id) { m_defaultPresetId = id; }
    void setDefaultPermissionMode(const QString &m) { m_defaultPermissionMode = m; }

    QString workspaceDir() const { return m_workspaceDir; }
    void setWorkspaceDir(const QString &d) { m_workspaceDir = d; }

    QString newId() const;

    static QString configDir();
    static QString configFile();

signals:
    void chatsChanged();
    void presetsChanged();
    void providersChanged();

private:
    Settings() = default;
    QVector<Chat> m_chats;
    QVector<Preset> m_presets;
    QVector<Provider> m_providers;
    QString m_defaultProviderId;
    QString m_defaultPresetId;
    QString m_defaultPermissionMode = "ask";
    QString m_workspaceDir;
};

}
