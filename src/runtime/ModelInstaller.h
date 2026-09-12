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

    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetsChanged)

    Q_PROPERTY(int searchCount READ searchCount NOTIFY searchChanged)
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)

    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY installedChanged)

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

    // --- accessors for properties -----------------------------------------
    int stateInt() const { return static_cast<int>(m_state); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    int installedCount() const { return m_installed.size(); }
    int presetCount() const { return m_presets.size(); }
    int searchCount() const { return m_searchResults.size(); }
    bool searchActive() const { return m_searchActive; }
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &q);
    QString activeTitle() const;

    Q_INVOKABLE void reloadPresets();
    Q_INVOKABLE QVariantMap installedInfo(int index) const;
    Q_INVOKABLE QString setActiveModel(int index);
    Q_INVOKABLE QString removeModel(int index);
    Q_INVOKABLE QString openModelFolder(int index);

    Q_INVOKABLE void rescanRegistry();

    Q_INVOKABLE QVariantMap presetInfo(int index) const;
    Q_INVOKABLE void preparePreset(int index);
    Q_INVOKABLE void installPrepared();
    Q_INVOKABLE void installRemote(int index);

    Q_INVOKABLE void startSearch();
    Q_INVOKABLE QVariantMap searchResult(int index) const;

    Q_INVOKABLE void cancelInstall();

    Q_INVOKABLE QString importCatalog(const QString &path);
    Q_INVOKABLE QString exportCatalog(const QString &path);
    Q_INVOKABLE QString resetUserCatalog();

    Q_INVOKABLE QString hfToken() const;
    Q_INVOKABLE void setHfToken(const QString &token);

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

    void refreshInstalled();
    bool isPresetInstalled(const ModelPreset &p) const;
    void beginPrepare(const ModelPreset &preset);
    void onPrepareDone(const Pending &p, const QString &err);

    void beginDownload();
    void enqueueFile(const QString &repoPath, const QString &repo,
                     const QString &commitSha);
    QString expectedShaFor(const QString &repoPath) const;
    bool mmprojAlreadyOnDisk() const;
    void onOneDownloadFinished(bool ok);
    void emitDownloadProgress();
    void maybeFinishDownloads();
    void completeInstall();

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    LaunchProfileStore &m_launchProfiles;
    RuntimePaths m_paths;
    DownloadManager *m_downloads = nullptr;

    State m_state = State::Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    QList<ModelPreset> m_presets;
    QList<ModelEntry> m_installed;

    QList<HfModelSummary> m_searchResults;
    QString m_searchQuery;
    bool m_searchActive = false;

    Pending m_pending;
    int m_downloadCount = 0;
    int m_downloadDone = 0;
    bool m_downloadFailed = false;

signals:
    void stateChanged();
    void busyChanged();
    void progressChanged();
    void statusMessageChanged();
    void installedChanged();
    void presetsChanged();
    void searchChanged();
};

}  // namespace llocr