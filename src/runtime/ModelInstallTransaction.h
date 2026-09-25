#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelRegistry.h"

class QJsonObject;

namespace llocr {

class DownloadGroup;
class DownloadManager;
class LaunchProfileStore;
class SettingsStore;

// The prepare → download → finalize pipeline for Hugging Face preset installs,
// extracted from ModelInstaller (which stays the QML façade: presets, registry
// and role-filtered views). The transaction owns the DownloadManager/Group,
// the pending install plan and the prepare generation gate; the façade mirrors
// its state/busy/progress/status via the signals below.
class ModelInstallTransaction : public QObject
{
    Q_OBJECT

public:
    // Mirrors ModelInstaller::State (same values); the façade maps it onto
    // its own Q_ENUM for QML.
    enum State { Idle = 0, Fetching = 1, ReadyToDownload = 2, Downloading = 3, Error = 4 };
    Q_ENUM(State)

    explicit ModelInstallTransaction(SettingsStore &settings,
                                     LaunchProfileStore &launchProfiles,
                                     QObject *parent = nullptr);
    ~ModelInstallTransaction() override;

    void shutdown();

    State state() const { return m_state; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    const QString &statusMessage() const { return m_statusMessage; }
    QString pendingTitle() const { return m_pending.title; }
    QString pendingRepo() const { return m_pending.repo; }

    // The registry snapshot the transaction merges into (role merge on
    // re-install, mmproj-reuse check). The façade keeps it in sync.
    const QList<ModelEntry> &installed() const { return m_installed; }
    void setInstalled(const QList<ModelEntry> &installed) { m_installed = installed; }

    void prepare(const ModelPreset &preset, bool forCheck);
    void installPrepared();
    void cancel();
    void retranslate();

    // Pure file-selection logic, exposed for tests.
    static QString repoDirName(const QString &repo);
    static void selectModelFiles(const QList<HfFile> &tree, const QString &prefer,
                                 const QString &preferMmproj, QStringList *modelPaths,
                                 QString &mmprojRel);

signals:
    void stateChanged(int state);
    void busyChanged(bool busy);
    void progressChanged(double progress);
    void statusMessageChanged(const QString &message);
    // Emitted after the registry was saved with the new install merged.
    void installedListReplaced(const QList<ModelEntry> &installed);
    void installFinished();

private:
    struct InstallPlan {
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

    void beginPrepare(const ModelPreset &preset);
    void onPrepareDone(const InstallPlan &p, const QString &err);
    void beginDownload();
    void enqueueFile(const QString &repoPath, const QString &repo,
                     const QString &commitSha);
    QString expectedShaFor(const QString &repoPath) const;
    bool mmprojAlreadyOnDisk() const;
    void maybeFinishDownloads();
    void completeInstall();

    SettingsStore &m_settings;
    LaunchProfileStore &m_launchProfiles;
    DownloadManager *m_downloads = nullptr;
    DownloadGroup *m_group = nullptr;

    State m_state = Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    InstallPlan m_pending;
    bool m_pendingForCheck = false;  // install auto-activates the check model
    int m_prepareGeneration = 0;
    QList<ModelEntry> m_installed;
};

}  // namespace llocr
