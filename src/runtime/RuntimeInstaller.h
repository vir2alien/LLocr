#pragma once

#include <QList>
#include <QDateTime>
#include <QLockFile>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "runtime/InstallTransaction.h"
#include "runtime/ReleaseAsset.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

class DownloadGroup;
class DownloadManager;
class SettingsStore;

class RuntimeInstaller : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)
    Q_PROPERTY(QString recommendedBackend READ recommendedBackend CONSTANT)
    Q_PROPERTY(QString recommendationReason READ recommendationReason CONSTANT)
    Q_PROPERTY(QStringList availableBackends READ availableBackends CONSTANT)

    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)

    Q_PROPERTY(int releaseCount READ releaseCount NOTIFY catalogChanged)
    Q_PROPERTY(int selectedRelease READ selectedRelease WRITE setSelectedRelease NOTIFY selectedReleaseChanged)

    Q_PROPERTY(QString installedBuild READ installedBuild NOTIFY installedChanged)
    Q_PROPERTY(QString installedBackend READ installedBackend NOTIFY installedChanged)
    Q_PROPERTY(bool hasUpdate READ hasUpdate NOTIFY hasUpdateChanged)

    Q_PROPERTY(int installedBuildCount READ installedBuildCount NOTIFY installedBuildsChanged)

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

    void retranslate();

    Q_INVOKABLE QString releaseLabel(int index) const;
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void startDownloadAndInstall();
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE QString cleanupUnusedBuilds();

    Q_INVOKABLE void rescanInstalledBuilds();
    Q_INVOKABLE QVariantMap installedBuildInfo(int index) const;
    Q_INVOKABLE QString activateBuild(int index);
    // Opens the install directory of the build in the system file manager;
    // returns an empty string on success or a localized error message.
    Q_INVOKABLE QString openBuildFolder(int index);
    Q_INVOKABLE static QString backendDisplayName(const QString &backend);

    Q_INVOKABLE QString updateBuild() const;
    Q_INVOKABLE QString updateTimestampLabel() const;
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void installUpdate();

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
    int installedBuildCount() const { return m_installedBuilds.size(); }
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
    static QString normalizedPath(const QString &path);

    void startCatalogFetch();
    void onCatalogLoaded(const QList<ReleaseInfo> &releases, const QString &error);
    void recomputeHasUpdate();

    void beginInstall(const QString &backend);
    void beginDownloads();
    void runInstallAsync();
    void onInstallFinished(const InstallOutput &out, const QString &warning);

    ReleaseAsset pickAsset(const ReleaseInfo &release, const QString &backend,
                           bool wantCudart) const;
    void maybeFinishDownloads();
    bool acquireInstallLock(QString &error);
    void releaseInstallLock();

signals:
    void stateChanged();
    void busyChanged();
    void backendChanged();
    void catalogChanged();
    void selectedReleaseChanged();
    void installedChanged();
    void installedBuildsChanged();
    void hasUpdateChanged();
    void progressChanged();
    void statusMessageChanged();

private:
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
    QDateTime m_lastCatalogAt;
    QList<InstalledBuildInfo> m_installedBuilds;

    QString m_pendingBackend;
    ReleaseAsset m_pendingMain;
    ReleaseAsset m_pendingCudart;
    bool m_pendingHasCudart = false;
    QString m_downloadedMainZip;
    QString m_downloadedCudartZip;

    DownloadManager *m_downloads = nullptr;
    DownloadGroup *m_group = nullptr;
    ::QLockFile m_installLock{ QStringLiteral("/") };
    bool m_installLockHeld = false;
};

}  // namespace llocr