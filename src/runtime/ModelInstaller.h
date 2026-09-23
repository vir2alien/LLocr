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

class DownloadGroup;
class DownloadManager;
class LaunchProfileStore;
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
    // Role-filtered counts as properties so QML bindings re-evaluate on
    // installedChanged (a Q_INVOKABLE call is not tracked by the engine).
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
    // Role-filtered views over the installed list: `index` addresses the
    // sublist of models matching the role, not the full registry. The info
    // map carries the full-list `index` for the action invokables.
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
    struct Pending {
        QString repo;
        QString revision;
        QString title;
        QString license;
        QString parser;
        QString prompt;
        int ctxSize = 0;
        QString presetId;
        QString dir;              // <modelsDir>/<org>__<repo>
        QString modelPath;        // absolute first part after install
        QString mmprojRel;        // repo-relative projector path, or empty
        QStringList modelNames;   // repo-relative model file paths (all parts)
        QHash<QString, QString> fileSha256;  // preset-pinned digest per file name (lowercased)
        QList<HfFile> files;      // full candidate file list for the repo
    };

    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);

    void reloadPresetsInternal();

    bool isPresetInstalled(const ModelPreset &p) const;
    bool matchesRole(const ModelEntry &e, bool forCheck) const;
    void beginPrepare(const ModelPreset &preset);
    void onPrepareDone(const Pending &p, const QString &err);

    void beginDownload();
    void enqueueFile(const QString &repoPath, const QString &repo,
                     const QString &commitSha);
    QString expectedShaFor(const QString &repoPath) const;
    bool mmprojAlreadyOnDisk() const;
    void maybeFinishDownloads();
    void completeInstall();

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    LaunchProfileStore &m_launchProfiles;
    DownloadManager *m_downloads = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presets;
    QList<ModelPreset> m_presetsValidate;
    QList<ModelEntry> m_installed;

    Pending m_pending;
    bool m_pendingForCheck = false;  // install auto-activates the check model
    int m_prepareGeneration = 0;

    DownloadGroup *m_group = nullptr;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
};

}  // namespace llocr