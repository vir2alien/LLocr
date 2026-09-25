#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelRegistry.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

class ModelInstallTransaction;
class RuntimeController;
class SettingsStore;

class ModelInstaller : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedChanged)
    Q_PROPERTY(int ocrInstalledCount READ ocrInstalledCount NOTIFY installedChanged)
    Q_PROPERTY(int checkInstalledCount READ checkInstalledCount NOTIFY installedChanged)

    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetsChanged)
    Q_PROPERTY(int checkPresetCount READ checkPresetCount NOTIFY presetsChanged)

    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY installedChanged)
    Q_PROPERTY(QString checkActiveTitle READ checkActiveTitle NOTIFY installedChanged)

public:
    enum State {
        Idle = 0,
        Fetching = 1,
        ReadyToDownload = 2,
        Downloading = 3,
        Error = 4,
    };
    Q_ENUM(State)

    explicit ModelInstaller(SettingsStore &settings, RuntimeController &runtime,
                            LaunchProfileStore &launchProfiles,
                            QObject *parent = nullptr);
    ~ModelInstaller() override;

    void shutdown();

    void retranslate();

    int stateInt() const { return static_cast<int>(m_state); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    int installedCount() const { return m_installed.size(); }
    int ocrInstalledCount() const { return roleInstalledCount(false); }
    int checkInstalledCount() const { return roleInstalledCount(true); }
    int presetCount() const { return m_presets.size(); }
    int checkPresetCount() const { return m_presetsValidate.size(); }
    QString activeTitle() const;
    QString checkActiveTitle() const;

    Q_INVOKABLE void reloadPresets();
    Q_INVOKABLE QVariantMap installedInfo(int index, bool forCheck = false) const;
    Q_INVOKABLE int roleInstalledCount(bool forCheck) const;
    Q_INVOKABLE QVariantMap roleInstalledInfo(int index, bool forCheck) const;
    Q_INVOKABLE QString setActiveModel(int index, bool forCheck = false);
    Q_INVOKABLE QString removeModel(int index);
    Q_INVOKABLE QString openModelFolder(int index);

    Q_INVOKABLE void refreshInstalled();
    Q_INVOKABLE void rescanRegistry();

    Q_INVOKABLE QVariantMap presetInfo(int index, bool forCheck = false) const;
    Q_INVOKABLE QString activatePreset(int index, bool forCheck = false);
    Q_INVOKABLE void preparePreset(int index, bool forCheck = false);
    Q_INVOKABLE void installPrepared();

    Q_INVOKABLE void cancelInstall();

private:
    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);

    void reloadPresetsInternal();

    bool isPresetInstalled(const ModelPreset &p) const;
    QString presetInstalledModelPath(const ModelPreset &p) const;
    bool matchesRole(const ModelEntry &e, bool forCheck) const;

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    LaunchProfileStore &m_launchProfiles;
    ModelInstallTransaction *m_transaction = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presets;
    QList<ModelPreset> m_presetsValidate;
    QList<ModelEntry> m_installed;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
};

}  // namespace llocr