#pragma once
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onProviderSelected(int row);
    void onPresetSelected(int row);
    void onAddProvider();
    void onRemoveProvider();
    void onAddPreset();
    void onRemovePreset();
    void saveCurrentProvider();
    void saveCurrentPreset();
    void accept() override;

private:
    void buildProvidersTab();
    void buildPresetsTab();
    void buildGeneralTab();
    void reloadProviders();
    void reloadPresets();

    QListWidget *m_providerList = nullptr;
    QLineEdit *m_pName = nullptr;
    QComboBox *m_pKind = nullptr;
    QLineEdit *m_pBase = nullptr;
    QLineEdit *m_pKey = nullptr;
    QPlainTextEdit *m_pModels = nullptr;
    QCheckBox *m_pImages = nullptr;
    QCheckBox *m_pEnabled = nullptr;
    QLabel *m_pBuiltin = nullptr;
    int m_currentProvider = -1;

    QListWidget *m_presetList = nullptr;
    QLineEdit *m_prName = nullptr;
    QComboBox *m_prProv = nullptr;
    QComboBox *m_prModel = nullptr;
    QPlainTextEdit *m_prSys = nullptr;
    QCheckBox *m_prTools = nullptr;
    QCheckBox *m_prImages = nullptr;
    QLineEdit *m_prTemp = nullptr;
    QLineEdit *m_prMax = nullptr;
    int m_currentPreset = -1;

    QComboBox *m_defaultProvider = nullptr;
    QComboBox *m_defaultPreset = nullptr;
    QComboBox *m_defaultPerm = nullptr;
    QLineEdit *m_workspace = nullptr;
};
