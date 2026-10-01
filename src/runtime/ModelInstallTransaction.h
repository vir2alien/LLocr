#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelPreset.h"
#include "runtime/ModelRegistry.h"
#include "runtime/StagedInstall.h"

class QJsonObject;

namespace llocr {

class InstalledState;

class DownloadGroup;
class DownloadManager;
class SettingsStore;

class ModelInstallTransaction : public QObject
{
    Q_OBJECT

public:
    enum State { Idle = 0, Fetching = 1, ReadyToDownload = 2, Downloading = 3, Error = 4 };
    Q_ENUM(State)

    explicit ModelInstallTransaction(SettingsStore &settings, InstalledState &state, QObject *parent = nullptr);
    ~ModelInstallTransaction() override;

    void shutdown();

    State state() const { return m_state; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    const QString &statusMessage() const { return m_statusMessage; }
    QString pendingTitle() const { return m_pending.title; }
    QString pendingRepo() const { return m_pending.repo; }

    const QList<ModelEntry> &installed() const { return m_installed; }
    void setInstalled(const QList<ModelEntry> &installed) { m_installed = installed; }

    void prepare(const ModelPreset &preset, bool forCheck);
    void installPrepared();
    void cancel();
    void retranslate();

    static QString repoDirName(const QString &repo);
    static void selectModelFiles(const QList<HfFile> &tree, const QString &prefer, const QString &preferMmproj, QStringList *modelPaths, QString &mmprojRel);
    static bool preserveExistingFiles(const QString &finalDir, const QString &stagingDir, const QStringList &writtenNames, QString *error = nullptr);

signals:
    void stateChanged(int state);
    void busyChanged(bool busy);
    void progressChanged(double progress);
    void statusMessageChanged(const QString &message);
    void installedListReplaced(const QList<llocr::ModelEntry> &installed);
    void installFinished();

private:
    struct InstallPlan {
        QString repo;
        QString revision;
        QString title;
        QString license;
        QString parser;
        int ctxSize = 0;
        QString presetId;
        QString dir;                         // <modelsDir>/<org>__<repo>
        QString modelPath;                   // absolute first part after install
        QString mmprojRel;                   // repo-relative projector path, or empty
        QStringList modelNames;              // repo-relative model file paths (all parts)
        QHash<QString, QString> fileSha256;  // preset-pinned digest per file name (lowercased)
        QList<HfFile> files;                 // full candidate file list for the repo
    };

    void setState(State next);
    void setBusy(bool busy);
    void setProgress(double p);
    void setStatusMessage(const QString &msg);

    void beginPrepare(const ModelPreset &preset);
    void onPrepareDone(const InstallPlan &p, const QString &err);
    void beginDownload();
    void enqueueModelFiles(bool mmprojOnDisk);
    void enqueueFile(const QString &repoPath, const QString &repo, const QString &commitSha);
    QString expectedShaFor(const QString &repoPath) const;
    static bool mmprojAlreadyOnDisk(const QString &dir, const QString &mmprojRel, const QString &expected, const QString &revision, const QList<ModelEntry> &installed);
    bool preservePendingFiles(QString *error);
    void maybeFinishDownloads();
    void completeInstall();
    void releaseInstallLock();

    SettingsStore &m_settings;
    InstalledState &m_installState;
    DownloadManager *m_downloads = nullptr;
    DownloadGroup *m_group = nullptr;

    State m_state = Idle;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusMessage;

    InstallPlan m_pending;
    QString m_installDir;
    std::unique_ptr<StagedInstall> m_staging;
    bool m_lockHeld = false;
    bool m_pendingForCheck = false;  // install auto-activates the check model
    int m_prepareGeneration = 0;
    QList<ModelEntry> m_installed;
};

}  // namespace llocr
