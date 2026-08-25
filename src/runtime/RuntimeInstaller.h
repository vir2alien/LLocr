#pragma once

#include <QList>
#include <QDateTime>
#include <QLockFile>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include "runtime/DownloadTask.h"
#include "runtime/InstallTransaction.h"
#include "runtime/ReleaseAsset.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

class DownloadManager;
class SettingsStore;

// Stage D UI controller: exposes the llama.cpp install flow to QML as a
// second managed-runtime singleton (`RuntimeInstaller`), complementing
// RuntimeController (process lifecycle) which stays focused on running the
// server. This class owns release-listing, backend recommendation, the
// download queue, the transactional install and "check for updates"/cleanup.
//
// All network and heavy on-disk work is async: catalog fetches and the atomic
// install run on QtConcurrent worker threads, downloads stream on the main
// thread via DownloadManager. QML only ever sees main-thread state via the
// properties below. Settings are written only by the commit step, after the
// install is already live (§7.2 / Stage D task 5).
//
// §H.6: install operations (download + install + cleanup) are guarded by a
// dedicated `.install.lock` (QLockFile) so that a second app instance can keep
// using External / browsing settings while runtime installs remain exclusive.
// A concurrent install attempt that fails to take the lock is refused with a
// clear status message instead of corrupting the runtime directory.
class RuntimeInstaller : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Install/flow state (see State enum below), exposed as int.
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    // True while catalog fetch / download / install is in flight.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    // Platform detection results.
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString recommendedBackend READ recommendedBackend CONSTANT)
    Q_PROPERTY(QString recommendationReason READ recommendationReason CONSTANT)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)

    // The backend selected via availableBackends; exposed so the picker can
    // both read and change it.
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)

    // Latest fetched release list.
    Q_PROPERTY(int releaseCount READ releaseCount NOTIFY catalogChanged)
    Q_PROPERTY(int selectedRelease READ selectedRelease WRITE setSelectedRelease NOTIFY selectedReleaseChanged)

    // What is currently installed.
    Q_PROPERTY(QString installedBuild READ installedBuild NOTIFY installedChanged)
    Q_PROPERTY(QString installedBackend READ installedBackend NOTIFY installedChanged)
    Q_PROPERTY(bool hasUpdate READ hasUpdate NOTIFY hasUpdateChanged)

    // Download/progress.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged)

public:
    enum State {
        Idle = 0,          // nothing loaded yet
        Fetching = 1,      // fetching the release catalog
        Ready = 2,         // catalog loaded, awaiting a user action
        Downloading = 3,   // downloading the archive(s)
        Installing = 4,    // verifying / extracting / probing
        Installed = 5,     // a build is installed and active
        Error = 6,         // last operation failed; statusMessage explains it
    };
    Q_ENUM(State)

    explicit RuntimeInstaller(SettingsStore &settings, QObject *parent = nullptr);
    ~RuntimeInstaller() override;

    void shutdown();

    // --- QML-facing API ----------------------------------------------------
    /// Returns a short human-readable label for a release row, e.g.
    /// "b10594 — 2026-03-12". Empty when the index is out of range.
    Q_INVOKABLE QString releaseLabel(int index) const;
    /// Returns the numeric build of a release row, or -1.
    Q_INVOKABLE int releaseBuild(int index) const;

    /// Kicks off a catalog fetch ("Check for updates" / opening the tab).
    Q_INVOKABLE void checkForUpdates();

    /// Starts download + install of `selectedRelease` for `backend()`.
    Q_INVOKABLE void startDownloadAndInstall();

    /// Cancels in-flight downloads (during the download phase only).
    Q_INVOKABLE void cancelInstall();

    /// Removes every installed build except the active one. Returns a summary
    /// or error message.
    Q_INVOKABLE QString cleanupUnusedBuilds();

    /// Resolves a backend's display name for results (e.g. "cuda" → "CUDA").
    Q_INVOKABLE static QString backendDisplayName(const QString &backend);

    // --- §H.3 update notification -----------------------------------------
    /// Build tag of the newest available release ("b10594"), empty when no
    /// release has been fetched or none is newer than the installed build.
    Q_INVOKABLE QString updateBuild() const;
    /// Human refresh label, e.g. "(updated 14:32)". Empty when unknown.
    Q_INVOKABLE QString updateTimestampLabel() const;
    /// Opens the latest release's page on GitHub in the default browser.
    Q_INVOKABLE void openReleasePage();
    /// Selects the newest fetched release and starts download + install (the
    /// “Update” action of the §H.3 plaque).
    Q_INVOKABLE void installUpdate();

    // --- accessors for properties -----------------------------------------
    int stateInt() const { return static_cast<int>(m_state); }
    bool busy() const { return m_busy; }
    QString platformLabel() const { return m_platformLabel; }
    QString recommendedBackend() const { return m_recommendedBackend; }
    QString recommendationReason() const { return m_recommendationReason; }
    QStringList availableBackends() const { return m_availableBackends; }
    QString backend() const { return m_backend; }
    int releaseCount() const { return m_releases.size(); }
    int selectedRelease() const { return m_selectedRelease; }
    QString installedBuild() const;
    QString installedBackend() const;
    bool hasUpdate() const { return m_hasUpdate; }
    double progress() const { return m_progress; }
    QString statusMessage() const { return m_statusMessage; }
    bool canInstall() const;

private:
    void setState(State next);
    void setBusy(bool busy);
    void setBackend(const QString &backend);
    void setSelectedRelease(int index);
    void setStatusMessage(const QString &msg);
    void setProgress(double p);

    void startCatalogFetch();
    void onCatalogLoaded(const QList<ReleaseInfo> &releases, const QString &error);

    // Pipeline steps, each invoked on the main thread.
    void beginInstall(const QString &backend);
    void beginDownloads();
    void runInstallAsync();
    void onInstallFinished(const InstallOutput &out, const QString &warning);

    // Best main asset for os/arch/backend (prefix match: "cuda" also matches
    // "cuda-cu12"); the cudart companion for CUDA builds.
    ReleaseAsset pickAsset(const ReleaseInfo &release, const QString &backend,
                           bool wantCudart) const;

    // Download-phase helpers.
    void enqueueDownload(const DownloadTask::Request &request);
    void onOneDownloadFinished(bool ok);
    void emitDownloadProgress();
    void maybeFinishDownloads();

    // §H.6 install exclusive-lock helpers (`.install.lock`).
    /// Tries to take the install lock; on success returns true. On failure
    /// sets `error` to a user-readable reason. Safe to call repeatedly.
    bool acquireInstallLock(QString &error);
    /// Releases the install lock if this instance currently holds it.
    void releaseInstallLock();

    SettingsStore &m_settings;
    RuntimePaths m_paths;

    State m_state = State::Idle;
    bool m_busy = false;
    QString m_statusMessage;
    double m_progress = 0.0;

    QString m_platformLabel;
    QString m_recommendedBackend;
    QString m_recommendationReason;
    QStringList m_availableBackends;
    QString m_backend;

    QList<ReleaseInfo> m_releases;
    int m_selectedRelease = 0;
    bool m_hasUpdate = false;
    QDateTime m_lastCatalogAt;   // when the catalog last loaded (§H.3 timestamp)

    QString m_pendingBackend;
    ReleaseAsset m_pendingMain;
    ReleaseAsset m_pendingCudart;
    bool m_pendingHasCudart = false;
    int m_pendingDownloadCount = 0;
    int m_pendingDownloadDone = 0;
    bool m_pendingDownloadFailed = false;
    QString m_downloadedMainZip;
    QString m_downloadedCudartZip;

    DownloadManager *m_downloads = nullptr;

    // §H.6: exclusive lock held for the whole install/cleanup duration.
    // Value member (QLockFile is not a QObject), path set in the ctor init-list.
    ::QLockFile m_installLock{ QStringLiteral("/") };
    bool m_installLockHeld = false;

signals:
    void stateChanged();
    void busyChanged();
    void backendChanged();
    void catalogChanged();
    void selectedReleaseChanged();
    void installedChanged();
    void hasUpdateChanged();
    void progressChanged();
    void statusMessageChanged();
};

}  // namespace llocr