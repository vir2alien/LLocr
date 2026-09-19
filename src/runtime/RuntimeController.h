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

public:
    explicit RuntimeController(SettingsStore &settings,
                               LaunchProfileStore &launchProfiles,
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

    Q_INVOKABLE static QString localPath(const QUrl &url)
    {
        return url.isLocalFile() ? url.toLocalFile() : url.toString();
    }

    Q_INVOKABLE QVariantMap estimateModelMemory(const QString &modelPath);
    Q_INVOKABLE bool canRecognize(bool documentLoaded) const;
    void ensureConnectionReady(const std::function<void(const ResolvedConnection &)> &onResolved);
    void ensureConnectionReady(QObject *context,
                               const std::function<void(const ResolvedConnection &)> &onResolved);
    void cancelPendingStart();
    void setSingleInstanceHeld(bool held);
    void bindSingleInstanceGuard(SingleInstanceGuard *guard);
    Q_INVOKABLE void refreshSingleInstanceLock();
    void setLogTarget(RuntimeLog *log);
    Q_INVOKABLE QString startServer();
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void restartServer();
    Q_INVOKABLE QString probeRuntimePath(const QString &path);
    Q_INVOKABLE QString launchCommandPreview();
    void shutdownSync();

    void retranslate();

    static ConnectionMode modeFromSettings(const SettingsStore &settings);

private:
    int stateInt() const { return static_cast<int>(m_state); }
    int busyStateInt() const { return static_cast<int>(m_busyState); }

    void setState(RuntimeState next);
    void setBusyState(AppBusyState next);
    void setStatusMessage(const QString &msg);
    void setLoadProgressPercent(int pct);

    void recomputeConfigValid();
    QString configNotReadyMessage() const;

    ResolvedConnection resolveExternal() const;
    ResolvedConnection buildManagedConnection() const;

    void beginManagedResolve();
    void completeResolve(ResolvedConnection conn);
    void failResolve(const QString &message);
    void onServerStateForResolve();
    void cancelPendingRestart();

    void fetchManagedModels();
    void onModelsReply(QNetworkReply *reply);

    QString describeServerFailure() const;
    static QString translateServerLine(const QString &line);

signals:
    void stateChanged();
    void busyStateChanged();
    void statusMessageChanged();
    void loadProgressChanged();
    void configValidChanged();
    void lockedOutChanged();

private:
    class LlamaServerProcess *m_server = nullptr;
    QMetaObject::Connection m_restartConn;
    RuntimeLog *m_logTarget = nullptr;
    SingleInstanceGuard *m_instanceGuard = nullptr;
    SettingsStore &m_settings;
    LaunchProfileStore &m_launchProfiles;
    struct PendingResolve {
        QPointer<QObject> context;
        bool guarded = false;
        std::function<void(const ResolvedConnection &)> onResolved;
    };
    std::vector<PendingResolve> m_resolveCallbacks;
    bool m_resolveInProgress = false;
    QNetworkAccessManager *m_modelsNet = nullptr;
    QString m_modelsBaseUrl;
    RuntimeState m_state = RuntimeState::NotConfigured;
    AppBusyState m_busyState = AppBusyState::Idle;
    QString m_statusMessage;
    int m_loadProgressPercent = -1;
    bool m_configValid = false;
    bool m_lockedOut = false;
};

}  // namespace llocr