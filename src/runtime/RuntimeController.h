#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <vector>

#include "runtime/ConnectionMode.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/ServerOwner.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

class SettingsStore;
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
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY lockedOutChanged)
    // A llama-server left running by a previous LLocr run (ADR 107). The record
    // is offered to the user, never killed without a click.
    Q_PROPERTY(bool orphanDetected READ orphanDetected NOTIFY orphanChanged)
    Q_PROPERTY(QString orphanInfo READ orphanInfo NOTIFY orphanChanged)

public:
    explicit RuntimeController(SettingsStore &settings,
                               LaunchProfileStore &launchProfiles,
                               LaunchProfileStore *checkLaunchProfiles = nullptr,
                               QObject *parent = nullptr);

    enum class AppBusyState {
        Idle,
        StartingRuntime,  // server is starting / loading the model
        Recognizing,
        StoppingRuntime,
        Downloading,      // model or runtime download in progress
        Installing,       // archive extraction / verification
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
    bool lockedOut() const { return m_lockedOut; }

    bool orphanDetected() const { return m_orphan.isValid(); }
    QString orphanInfo() const;
    /// Terminates the detected orphan (after re-validating it) and drops the
    /// record. Returns an empty string on success, a user-facing reason otherwise.
    Q_INVOKABLE QString terminateOrphan();

    Q_INVOKABLE static QString localPath(const QUrl &url)
    {
        return url.isLocalFile() ? url.toLocalFile() : url.toString();
    }

    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath);
    Q_INVOKABLE bool canRecognize(bool documentLoaded) const;
    void ensureConnectionReady(const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(QObject *context,
                               const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(ConnectionRole role,
                               const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(QObject *context, ConnectionRole role,
                               const std::function<void(const ResolvedConnection &)> &onResolved);
    void cancelPendingStart();
    void setSingleInstanceHeld(bool held);
    void bindSingleInstanceGuard(SingleInstanceGuard *guard);
    Q_INVOKABLE void refreshSingleInstanceLock();
    void setLogTarget(RuntimeLog *log);
    // Re-reads <rootDir>/owner.json. Called at construction; the paths follow
    // the current settings, so a moved runtime directory is re-scanned.
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

    void setState(RuntimeState next);
    void setBusyState(AppBusyState next);
    void setStatusMessage(const QString &msg);
    void setLoadProgressPercent(int pct);

    void recomputeConfigValid();

    QString roleModelPath(ConnectionRole role) const;
    QString roleMmprojPath(ConnectionRole role) const;
    bool serverRunsRole(ConnectionRole role) const;
    QString roleConfigError(ConnectionRole role) const;
    void beginRoleSwitch();

    ResolvedConnection resolveExternal(ConnectionRole role = ConnectionRole::Ocr) const;
    ResolvedConnection buildManagedConnection() const;

    void beginManagedResolve();
    void startResolveForRole(ConnectionRole role);
    void completeResolve(ResolvedConnection conn);
    void failResolve(const QString &message);
    void deliverCallbacks(const std::vector<PendingResolve> &callbacks,
                          const ResolvedConnection &conn);
    void onServerStateForResolve();
    void cancelPendingRestart();

    void fetchManagedModels();
    void onModelsReply(QNetworkReply *reply);

    QString startServer(ConnectionRole role);
    void finishStartServer(ConnectionRole role, const QString &program,
                           const ProbeResult &probe);
    QString describeServerFailure() const;
    static QString translateServerLine(const QString &line);

signals:
    void stateChanged();
    void busyStateChanged();
    void statusMessageChanged();
    void loadProgressChanged();
    void configValidChanged();
    void lockedOutChanged();
    void orphanChanged();

private:
    class LlamaServerProcess *m_server = nullptr;
    QMetaObject::Connection m_restartConn;
    RuntimeLog *m_logTarget = nullptr;
    SingleInstanceGuard *m_instanceGuard = nullptr;
    SettingsStore &m_settings;
    LaunchProfileStore &m_launchProfiles;
    LaunchProfileStore *m_checkLaunchProfiles = nullptr;
    struct PendingResolve {
        QPointer<QObject> context;
        bool guarded = false;
        ConnectionRole role = ConnectionRole::Ocr;
        std::function<void(const ResolvedConnection &)> onResolved;
    };
    // Callbacks of the resolve that is in flight — all of them asked for the
    // same role, so one connection serves them all.
    std::vector<PendingResolve> m_resolveCallbacks;
    // Requests for the *other* role that arrived while a resolve was in flight.
    // The single managed server serves one role at a time (ADR 74), so joining
    // the in-flight batch would silently answer with the wrong model; they wait
    // here for their own dispatch (a role switch when the server is live).
    std::vector<PendingResolve> m_deferredResolves;
    ConnectionRole m_resolveRole = ConnectionRole::Ocr;
    bool m_switching = false;
    bool m_resolveInProgress = false;
    QNetworkAccessManager *m_modelsNet = nullptr;
    QString m_modelsBaseUrl;
    QString m_startedModelPath;
    QString m_startedMmprojPath;
    RuntimeState m_state = RuntimeState::NotConfigured;
    AppBusyState m_busyState = AppBusyState::Idle;
    QString m_statusMessage;
    int m_loadProgressPercent = -1;
    bool m_configValid = false;
    bool m_lockedOut = false;
    ServerOwnerRecord m_orphan;
    QString m_orphanJsonPath;
    // Bumped for every started probe; a result whose generation is stale
    // (cancelled, or superseded by a newer probe) is dropped (ADR 105).
    quint64 m_startGeneration = 0;
};

}  // namespace llocr