#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <vector>

#include "config/RuntimePaths.h"
#include "runtime/ConnectionMode.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/ServerLaunchConfig.h"
#include "runtime/ServerOwner.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

class SettingsStore;
class InstalledState;
class LaunchProfileStore;
class RuntimeLog;
class SingleInstanceGuard;

class RuntimeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(int busyState READ busyStateInt NOTIFY busyStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(int loadProgressPercent READ loadProgressPercent NOTIFY loadProgressChanged)
    Q_PROPERTY(bool configValid READ configValid NOTIFY configValidChanged)
    Q_PROPERTY(bool serverPathValid READ serverPathValid NOTIFY configValidChanged)
    Q_PROPERTY(bool modelPathValid READ modelPathValid NOTIFY configValidChanged)
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY lockedOutChanged)
    Q_PROPERTY(bool launchConfigDirty READ launchConfigDirty NOTIFY launchConfigDirtyChanged)
    Q_PROPERTY(bool orphanDetected READ orphanDetected NOTIFY orphanChanged)
    Q_PROPERTY(QString orphanInfo READ orphanInfo NOTIFY orphanChanged)

public:
    explicit RuntimeController(SettingsStore &settings, LaunchProfileStore &launchProfiles, InstalledState *state = nullptr, QObject *parent = nullptr);

    enum class AppBusyState {
        Idle,
        StartingRuntime,  // server is starting / loading the model
        Recognizing,
        StoppingRuntime,
        Downloading,  // model or runtime download in progress
        Installing,   // archive extraction / verification
    };
    Q_ENUM(AppBusyState)

    enum class RuntimeState {
        NotConfigured,  // no valid binary / no model selected
        Stopped,
        Starting,
        Ready,
        Stopping,
        Failed,
    };
    Q_ENUM(RuntimeState)

    RuntimeState state() const { return m_state; }
    AppBusyState busyState() const { return m_busyState; }
    QString statusMessage() const { return m_statusMessage; }
    int loadProgressPercent() const { return m_loadProgressPercent; }
    bool configValid() const { return m_configValid; }
    bool serverPathValid() const { return m_serverPathValid; }
    bool modelPathValid() const { return m_modelPathValid; }
    bool lockedOut() const { return m_lockedOut; }
    bool launchConfigDirty() const { return m_launchConfigDirty; }

    bool orphanDetected() const { return m_orphan.isValid(); }
    QString orphanInfo() const;
    Q_INVOKABLE QString terminateOrphan();

    Q_INVOKABLE static QString localPath(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }

    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath, bool forCheck = false);
    Q_INVOKABLE bool canRecognize(bool documentLoaded) const;
    void ensureConnectionReady(const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(QObject *context, const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(QObject *context, ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved);
    void cancelPendingStart();
    void setSingleInstanceHeld(bool held);
    void bindSingleInstanceGuard(SingleInstanceGuard *guard);
    Q_INVOKABLE void refreshSingleInstanceLock();
    void setLogTarget(RuntimeLog *log);
    void scanForOrphanedServer();
    Q_INVOKABLE QString startServer();
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void restartServer();
    Q_INVOKABLE QString probeRuntimePath(const QString &path);
    Q_INVOKABLE QString launchCommandPreview();
    void shutdownSync();

    void retranslate();

    static ConnectionMode modeFromSettings(const SettingsStore &settings);

private:
    struct PendingResolve;

    int stateInt() const { return static_cast<int>(m_state); }
    int busyStateInt() const { return static_cast<int>(m_busyState); }

    RuntimePaths currentPaths() const;
    void setState(RuntimeState next);
    void setBusyState(AppBusyState next);
    void setStatusMessage(const QString &msg);
    void setLoadProgressPercent(int pct);

private slots:
    void recomputeConfigValid();
    void recomputeLaunchConfigDirty();

private:
    QString roleModelPath(ConnectionRole role) const;
    QString roleMmprojPath(ConnectionRole role) const;
    bool serverRunsRole(ConnectionRole role) const;
    QString roleConfigError(ConnectionRole role) const;
    QString modelBuildError(ConnectionRole role) const;
    void beginRoleSwitch();

    ResolvedConnection resolveExternal(ConnectionRole role = ConnectionRole::Ocr) const;
    ResolvedConnection buildManagedConnection() const;

    void beginManagedResolve();
    void startResolveForRole(ConnectionRole role);
    void completeResolve(ResolvedConnection conn);
    void failResolve(const QString &message);
    void deliverCallbacks(const std::vector<PendingResolve> &callbacks, const ResolvedConnection &conn);
    void onServerStateForResolve();
    void cancelPendingRestart();

    void fetchManagedModels();
    void onModelsReply(QNetworkReply *reply);

    QString startServer(ConnectionRole role);
    void finishStartServer(ConnectionRole role, const QString &program, const ProbeResult &probe);
    QString describeServerFailure() const;
    static QString translateServerLine(const QString &line);

signals:
    void stateChanged();
    void busyStateChanged();
    void statusMessageChanged();
    void loadProgressChanged();
    void configValidChanged();
    void launchConfigDirtyChanged();
    void lockedOutChanged();
    void orphanChanged();

private:
    class LlamaServerProcess *m_server = nullptr;
    QMetaObject::Connection m_restartConn;
    RuntimeLog *m_logTarget = nullptr;
    SingleInstanceGuard *m_instanceGuard = nullptr;
    SettingsStore &m_settings;
    InstalledState *m_installedState = nullptr;
    LaunchProfileStore &m_launchProfiles;
    struct PendingResolve {
        QPointer<QObject> context;
        bool guarded = false;
        ConnectionRole role = ConnectionRole::Ocr;
        std::function<void(const ResolvedConnection &)> onResolved;
    };
    std::vector<PendingResolve> m_resolveCallbacks;
    std::vector<PendingResolve> m_deferredResolves;
    ConnectionRole m_resolveRole = ConnectionRole::Ocr;
    bool m_switching = false;
    bool m_resolveInProgress = false;
    QNetworkAccessManager *m_modelsNet = nullptr;
    QString m_modelsBaseUrl;
    ServerLaunchConfig m_startedConfig;
    ConnectionRole m_startedRole = ConnectionRole::Ocr;
    bool m_hasStartedConfig = false;
    RuntimeState m_state = RuntimeState::NotConfigured;
    AppBusyState m_busyState = AppBusyState::Idle;
    QString m_statusMessage;
    int m_loadProgressPercent = -1;
    bool m_configValid = false;
    bool m_serverPathValid = false;
    bool m_modelPathValid = false;
    bool m_launchConfigDirty = false;
    bool m_lockedOut = false;
    ServerOwnerRecord m_orphan;
    QString m_orphanJsonPath;
    quint64 m_startGeneration = 0;
};

}  // namespace llocr