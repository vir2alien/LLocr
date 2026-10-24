#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaMethod>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>
#include <QVariantMap>

#include <utility>

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ByteSize.h"
#include "core/LaunchProfile.h"
#include "core/ModelProfiles.h"
#include "runtime/HardwareInfo.h"
#include "runtime/HttpClient.h"
#include "runtime/InstalledState.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/ProcessGuard.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimeLog.h"
#include "runtime/ServerCapabilities.h"
#include "runtime/ServerLaunchConfig.h"
#include "runtime/ServerOwner.h"
#include "runtime/SingleInstanceGuard.h"

namespace llocr {

namespace {
constexpr int kModelsRequestTimeoutMs = 10000;  // /v1/models query
constexpr int kProbeTimeoutMs = 120000;         // RuntimeLocator::probeCached (cold Metal cache)
constexpr int kShutdownTimeoutMs = 5000;        // shutdownSync grace
constexpr double kMemoryBudgetFactor = 0.9;

void connectEveryChangeSignal(QObject *source, QObject *receiver, const char *slot)
{
    const QString slotName = QString::fromLatin1(slot);
    const QMetaObject *receiverMeta = receiver->metaObject();
    QMetaMethod target;
    for (int i = 0; i < receiverMeta->methodCount(); ++i) {
        const QMetaMethod candidate = receiverMeta->method(i);
        if (candidate.methodType() == QMetaMethod::Slot && slotName == QLatin1String(candidate.name())) {
            target = candidate;
            break;
        }
    }
    if (!target.isValid()) {
        qWarning("connectEveryChangeSignal: %s has no slot %s", qUtf8Printable(QString::fromLatin1(source->metaObject()->className())), slot);
        return;
    }

    const QMetaObject *meta = source->metaObject();
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Signal)
            continue;
        if (method.parameterCount() != 0)
            continue;
        if (!QLatin1String(method.name()).endsWith(QLatin1String("Changed")))
            continue;
        QObject::connect(source, method, receiver, target);
    }
}
}  // namespace

RuntimeController::RuntimeController(SettingsStore &settings, LaunchProfileStore &launchProfiles, InstalledState *state, QObject *parent)
    : QObject(parent), m_settings(settings), m_installedState(state), m_launchProfiles(launchProfiles)
{
    recomputeConfigValid();
    recomputeLaunchConfigDirty();
    recomputeTaskModels();
    connectEveryChangeSignal(&m_settings, this, "recomputeConfigValid");
    connectEveryChangeSignal(&m_settings, this, "recomputeLaunchConfigDirty");
    connectEveryChangeSignal(&m_settings, this, "invalidateResolvedCache");
    connectEveryChangeSignal(&m_settings, this, "recomputeTaskModels");
    connect(&m_settings, &SettingsStore::runtimeRootDirChanged, this, &RuntimeController::scanForOrphanedServer);
    connect(&m_settings, &SettingsStore::runtimeModelsDirChanged, this, &RuntimeController::scanForOrphanedServer);
    // A saved launch profile is a launch-setting change like any other.
    connect(&m_launchProfiles, &LaunchProfileStore::profileChanged, this, &RuntimeController::recomputeLaunchConfigDirty);
    scanForOrphanedServer();
}

QString RuntimeController::orphanInfo() const
{
    if (!m_orphan.isValid())
        return {};
    return tr("A llama-server from a previous LLocr run is still running "
              "(pid %1, port %2).")
        .arg(m_orphan.pid)
        .arg(m_orphan.port);
}

void RuntimeController::scanForOrphanedServer()
{
    const RuntimePaths paths = m_installedState ? m_installedState->paths() : currentPaths();
    const QString ownerPath = QDir(paths.runtimeDir()).filePath(QStringLiteral("owner.json"));
    m_orphanJsonPath = ownerPath;

    ServerOwnerRecord found;
    const bool orphan = ServerOwner::findOrphan(ownerPath, &found);
    if (orphan == m_orphan.isValid() && (!orphan || found.pid == m_orphan.pid))
        return;

    m_orphan = orphan ? found : ServerOwnerRecord{};
    emit orphanChanged();
    if (orphan)
        qWarning().noquote() << orphanInfo();
}

QString RuntimeController::terminateOrphan()
{
    if (!m_orphan.isValid())
        return tr("No orphaned server to terminate");
    if (!ServerOwner::isOrphan(m_orphan))
        return tr("The orphaned server is already gone");
    if (!ProcessGuard::terminateProcess(m_orphan.pid))
        return tr("Unable to terminate the process (pid %1)").arg(m_orphan.pid);
    ServerOwner::clear(m_orphanJsonPath);
    m_orphan = ServerOwnerRecord{};
    emit orphanChanged();
    return {};
}

RuntimePaths RuntimeController::currentPaths() const
{
    return RuntimePaths::fromSettings(m_settings);
}

void RuntimeController::setSingleInstanceHeld(bool held)
{
    if (m_lockedOut == held)
        return;
    m_lockedOut = held;
    if (held)
        setStatusMessage(tr("Another LLocr instance is already running; "
                            "local server operations are disabled."));
    emit lockedOutChanged();
}

void RuntimeController::bindSingleInstanceGuard(SingleInstanceGuard *guard)
{
    m_instanceGuard = guard;
}

void RuntimeController::refreshSingleInstanceLock()
{
    if (!m_instanceGuard)
        return;
    QString error;
    const bool soleInstance = m_instanceGuard->tryAcquire(error);
    setSingleInstanceHeld(!soleInstance);
}

void RuntimeController::setLogTarget(RuntimeLog *log)
{
    m_logTarget = log;
}

void RuntimeController::setState(RuntimeState next)
{
    if (m_state == next)
        return;
    m_state = next;
    m_resolvedCache.clear();
    if (next != RuntimeState::Starting && next != RuntimeState::Ready) {
        m_hasStartedConfig = false;
        if (m_launchConfigDirty) {
            m_launchConfigDirty = false;
            emit launchConfigDirtyChanged();
        }
    }
    emit stateChanged();
}

void RuntimeController::setBusyState(AppBusyState next)
{
    if (m_busyState == next)
        return;
    m_busyState = next;
    emit busyStateChanged();
}

void RuntimeController::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void RuntimeController::setLoadProgressPercent(int pct)
{
    if (m_loadProgressPercent == pct)
        return;
    m_loadProgressPercent = pct;
    emit loadProgressChanged();
}

void RuntimeController::recomputeConfigValid()
{
    const QString program = m_settings.serverPath().trimmed();
    const bool serverOk = !program.isEmpty() && QFileInfo(program).isFile();
    const QString model = m_settings.launchModelPath().trimmed();
    const bool modelOk = !model.isEmpty() && QFileInfo(model).isFile();

    const bool valid = serverOk && (modeFromSettings(m_settings) == ConnectionMode::External || modelOk);

    const bool changed = valid != m_configValid || serverOk != m_serverPathValid || modelOk != m_modelPathValid;
    m_configValid = valid;
    m_serverPathValid = serverOk;
    m_modelPathValid = modelOk;
    if (changed)
        emit configValidChanged();
    if (valid && m_state == RuntimeState::NotConfigured)
        setState(RuntimeState::Stopped);
    else if (!valid && m_state == RuntimeState::Stopped)
        setState(RuntimeState::NotConfigured);
}

void RuntimeController::recomputeLaunchConfigDirty()
{
    ServerLaunchConfig current = ServerLaunchConfig::fromSettings(m_settings, m_launchProfiles, m_startedRole);
    current.program = m_settings.serverPath().trimmed();
    const bool dirty = m_hasStartedConfig && current != m_startedConfig;
    if (dirty == m_launchConfigDirty)
        return;
    m_launchConfigDirty = dirty;
    emit launchConfigDirtyChanged();
}

void RuntimeController::invalidateResolvedCache()
{
    m_resolvedCache.clear();
}

ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
{
    return settings.mode();
}

QString RuntimeController::roleModelPath(ConnectionRole role) const
{
    return m_settings.launchModelPathForRole(connectionRoleName(role)).trimmed();
}

QString RuntimeController::roleMmprojPath(ConnectionRole role) const
{
    return m_settings.launchMmprojPathForRole(connectionRoleName(role)).trimmed();
}

bool RuntimeController::serverRunsRole(ConnectionRole role) const
{
    const QString want = roleModelPath(role);
    if (want.isEmpty())
        return true;
    if (m_startedConfig.modelPath.trimmed() != want || m_startedConfig.mmprojPath.trimmed() != roleMmprojPath(role))
        return false;
    if (role == m_startedRole)
        return true;  // the running server is this role's own; changed launch settings are the restart banner's business

    // One model for both roles: the same weights are already loaded, and the
    // request parameters travel in the request body, so only the startup flags
    // can force a reload. Comparing them is what keeps the same model from being
    // loaded again between recognition and verification — and what makes a role
    // whose own layer differs (a wider context, a different KV type, a vision
    // budget) get the server it asked for instead of the other role's.
    // The alias is a label and stays out: the running one is what
    // buildManagedConnection() reports and the request names.
    ServerLaunchConfig forRole = ServerLaunchConfig::fromSettings(m_settings, m_launchProfiles, role);
    forRole.program = m_startedConfig.program;
    forRole.modelAlias = m_startedConfig.modelAlias;
    return forRole == m_startedConfig;
}

QString RuntimeController::roleConfigError(ConnectionRole role) const
{
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return tr("Managed server is not configured");
    if (!QFileInfo(program).isFile())
        return tr("File not found: %1").arg(program);
    if (modeFromSettings(m_settings) != ConnectionMode::Managed)
        return QString();

    const QString model = roleModelPath(role);
    if (const QString buildError = modelBuildError(role); !buildError.isEmpty())
        return buildError;
    switch (role) {
    case ConnectionRole::PageOcr:
        if (model.isEmpty())
            return tr("Model is not selected — pick a model in Settings → Models "
                      "or in the Setup wizard");
        if (!QFileInfo(model).isFile())
            return tr("Model file not found: %1 — re-select the model in Settings → Models").arg(model);
        return QString();
    case ConnectionRole::Decision:
        if (model.isEmpty())
            return tr("Decision model is not selected — pick a model in Settings → Decision model");
        if (!QFileInfo(model).isFile())
            return tr("Decision model file not found: %1 — re-select the model in "
                      "Settings → Decision model")
                .arg(model);
        return QString();
    case ConnectionRole::Layout:
        if (model.isEmpty())
            return tr("Layout model is not selected — pick a model in Settings → Layout model");
        if (!QFileInfo(model).isFile())
            return tr("Layout model file not found: %1 — re-select the model in "
                      "Settings → Layout model")
                .arg(model);
        return QString();
    case ConnectionRole::RawPageOcr:
        if (model.isEmpty())
            return tr("Raw OCR model is not selected — pick a model in Settings → Models");
        if (!QFileInfo(model).isFile())
            return tr("Raw OCR model file not found: %1 — re-select the model in "
                      "Settings → Models")
                .arg(model);
        return QString();
    case ConnectionRole::Fix:
        if (model.isEmpty())
            return tr("Fix model is not selected — pick a model in Settings → Models");
        if (!QFileInfo(model).isFile())
            return tr("Fix model file not found: %1 — re-select the model in "
                      "Settings → Models")
                .arg(model);
        return QString();
    case ConnectionRole::BlockRecognition:
        break;
    }
    if (model.isEmpty())
        return tr("Block OCR model is not selected — pick a model in Settings → Block OCR model");
    if (!QFileInfo(model).isFile())
        return tr("Block OCR model file not found: %1 — re-select the model in "
                  "Settings → Block OCR model")
            .arg(model);
    return QString();
}

QString RuntimeController::modelBuildError(ConnectionRole role) const
{
    const QString roleName = connectionRoleName(role);
    const QString modelId = m_settings.requestProfileIdForRole(roleName);
    const ModelProfiles::Role *modelRole = ModelProfiles::roleFor(modelId, roleName);
    const ModelProfiles::Profile *profile = ModelProfiles::findBuiltIn(modelId);
    const QString minBuild = profile ? profile->minBuild : QString();
    if (minBuild.isEmpty())
        return QString();

    ProbeResult probe;
    RuntimeLocator::cachedProbe(m_settings.serverPath(), currentPaths().cacheDir(), probe);
    if (!probe.ok || !probe.capabilities.ok)
        return QString();  // the binary says nothing; the start reports that
    if (ServerCapabilities::buildAtLeast(probe.capabilities.build, minBuild))
        return QString();

    return tr("%1 needs llama.cpp %2 or newer (this build is %3) — update the "
              "runtime in Settings → Runtime")
        .arg(modelRole ? modelRole->alias : modelId, minBuild, probe.capabilities.build);
}

bool RuntimeController::canRecognize(bool documentLoaded) const
{
    if (!documentLoaded)
        return false;
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return true;
    if (m_state == RuntimeState::Ready)
        return true;
    return m_state == RuntimeState::Stopped && m_configValid && (m_settings.autoStart() || m_settings.startOnDemand());
}

bool RuntimeController::taskModelAvailable(const QString &task) const
{
    return taskModelAvailable(connectionRoleFromName(task));
}

bool RuntimeController::taskModelAvailable(ConnectionRole role) const
{
    if (modeFromSettings(m_settings) == ConnectionMode::Managed)
        return QFileInfo::exists(m_settings.launchModelPathForRole(connectionRoleName(role)));
    return !m_settings.modelNameForRole(connectionRoleName(role)).trimmed().isEmpty();
}

void RuntimeController::recomputeTaskModels()
{
    std::array<bool, connectionRoleCount> fresh = {};
    for (const ConnectionRole role : connectionRoles)
        fresh[static_cast<std::size_t>(role)] = taskModelAvailable(role);
    if (fresh == m_taskModels)
        return;
    m_taskModels = fresh;
    emit taskModelsChanged();
}

ResolvedConnection RuntimeController::resolveExternal(ConnectionRole role) const
{
    ResolvedConnection conn;
    conn.baseUrl = m_settings.baseUrl();
    conn.apiKey = m_settings.apiKey();
    conn.modelId = m_settings.modelNameForRole(connectionRoleName(role));
    conn.connectionTimeoutMs = m_settings.connectionTimeoutMs();
    conn.responseTimeoutMs = m_settings.responseTimeoutMs();
    return conn;
}

void RuntimeController::ensureConnectionReady(const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(nullptr, ConnectionRole::PageOcr, onResolved);
}

void RuntimeController::ensureConnectionReady(QObject *context, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(context, ConnectionRole::PageOcr, onResolved);
}

void RuntimeController::ensureConnectionReady(ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(nullptr, role, onResolved);
}

void RuntimeController::ensureConnectionReady(QObject *context, ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        onResolved(resolveExternal(role));
        return;
    }

    // Steady state: the server is Ready on this role and nothing that feeds
    // the connection has changed since the last resolve. Answering the
    // per-block verification calls from the cache keeps them off the
    // /v1/models round-trip; every state transition or settings change
    // clears it, and an in-flight resolve follows the queue below.
    if (!m_resolveInProgress && !m_switching && m_state == RuntimeState::Ready && !m_lockedOut && serverRunsRole(role)) {
        const auto cached = m_resolvedCache.find(role);
        if (cached != m_resolvedCache.end()) {
            onResolved(cached->second);
            return;
        }
    }

    PendingResolve pending;
    pending.context = context;
    pending.guarded = context != nullptr;
    pending.role = role;
    pending.onResolved = onResolved;

    if (m_resolveInProgress) {
        auto &queue = role == m_resolveRole ? m_resolveCallbacks : m_deferredResolves;
        queue.push_back(std::move(pending));
        return;
    }

    m_resolveCallbacks.push_back(std::move(pending));
    startResolveForRole(role);
}

void RuntimeController::startResolveForRole(ConnectionRole role)
{
    m_resolveInProgress = true;
    m_resolveRole = role;

    if (m_lockedOut) {
        failResolve(tr("Another LLocr instance is already running"));
        return;
    }

    const bool serverLive = m_state == RuntimeState::Ready || m_state == RuntimeState::Starting;
    const bool switchNeeded = serverLive && !serverRunsRole(role);

    if (!serverLive || switchNeeded) {
        const QString roleError = roleConfigError(role);
        if (!roleError.isEmpty()) {
            failResolve(roleError);
            return;
        }
        if (!serverLive && m_state != RuntimeState::Stopping && !m_settings.autoStart() && !m_settings.startOnDemand()) {
            failResolve(tr("Server is not set to start automatically. "
                           "Start it from the main window or Settings → Runtime."));
            return;
        }
    }

    if (switchNeeded) {
        beginRoleSwitch();
        return;
    }
    beginManagedResolve();
}

void RuntimeController::beginManagedResolve()
{
    switch (m_state) {
    case RuntimeState::Ready:
        fetchManagedModels();
        break;
    case RuntimeState::Starting:
    case RuntimeState::Stopping:
        break;
    case RuntimeState::NotConfigured: {
        const QString roleError = roleConfigError(m_resolveRole);
        if (!roleError.isEmpty()) {
            failResolve(roleError);
            break;
        }
        Q_FALLTHROUGH();
    }
    case RuntimeState::Stopped:
    case RuntimeState::Failed: {
        setBusyState(AppBusyState::StartingRuntime);
        const QString err = startServer(m_resolveRole);
        if (!err.isEmpty()) {
            setBusyState(AppBusyState::Idle);
            failResolve(err);
        }
        break;
    }
    }  // switch (m_state)
}

void RuntimeController::deliverCallbacks(const std::vector<PendingResolve> &callbacks, const ResolvedConnection &conn)
{
    for (const auto &cb : callbacks) {
        if (cb.guarded && cb.context.isNull())
            continue;
        cb.onResolved(conn);
    }
}

void RuntimeController::completeResolve(ResolvedConnection conn)
{
    m_switching = false;
    if (!m_resolveInProgress)
        return;
    m_resolveInProgress = false;
    if (conn.error.isEmpty() && !conn.baseUrl.isEmpty())
        m_resolvedCache[m_resolveRole] = conn;
    const auto callbacks = std::move(m_resolveCallbacks);
    m_resolveCallbacks.clear();
    deliverCallbacks(callbacks, conn);

    if (m_resolveInProgress || m_deferredResolves.empty())
        return;

    const ConnectionRole nextRole = m_deferredResolves.front().role;
    for (const auto &cb : std::as_const(m_deferredResolves)) {
        if (cb.role == nextRole)
            m_resolveCallbacks.push_back(cb);
    }
    m_deferredResolves.erase(std::remove_if(m_deferredResolves.begin(), m_deferredResolves.end(), [nextRole](const PendingResolve &cb) { return cb.role == nextRole; }), m_deferredResolves.end());

    startResolveForRole(nextRole);
}

void RuntimeController::failResolve(const QString &message)
{
    ResolvedConnection fail;
    fail.error = message;
    completeResolve(std::move(fail));
}

void RuntimeController::onServerStateForResolve()
{
    if (!m_resolveInProgress) {
        m_switching = false;
        if (m_state == RuntimeState::Ready || m_state == RuntimeState::Stopped || m_state == RuntimeState::Failed)
            setBusyState(AppBusyState::Idle);
        return;
    }
    if (m_state == RuntimeState::Ready) {
        fetchManagedModels();
    } else if (m_state == RuntimeState::Failed) {
        if (m_switching) {
            m_switching = false;
            beginManagedResolve();
            return;
        }
        setBusyState(AppBusyState::Idle);
        failResolve(describeServerFailure());
    } else if (m_state == RuntimeState::Stopping || m_state == RuntimeState::Stopped) {
        if (m_switching) {
            if (m_state == RuntimeState::Stopped) {
                m_switching = false;
                beginManagedResolve();
            }
            return;
        }
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server stopped"));
    }
}

void RuntimeController::beginRoleSwitch()
{
    m_switching = true;
    setBusyState(AppBusyState::StartingRuntime);
    setLoadProgressPercent(-1);
    setStatusMessage(m_resolveRole == ConnectionRole::PageOcr      ? tr("Switching to the OCR model…")
                     : m_resolveRole == ConnectionRole::RawPageOcr ? tr("Switching to the raw OCR model…")
                     : m_resolveRole == ConnectionRole::Decision   ? tr("Switching to the decision model…")
                     : m_resolveRole == ConnectionRole::Layout     ? tr("Switching to the layout model…")
                     : m_resolveRole == ConnectionRole::Fix        ? tr("Switching to the fix model…")
                                                                   : tr("Switching to the block OCR model…"));
    if (!m_server) {
        m_switching = false;
        beginManagedResolve();
        return;
    }
    m_server->stop();  // Stopped → onServerStateForResolve → beginManagedResolve
}

ResolvedConnection RuntimeController::buildManagedConnection() const
{
    ResolvedConnection conn;
    const int port = m_server ? m_server->resolvedPort() : m_settings.launchPort();
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_settings.launchHost());
    url.setPort(port);
    conn.baseUrl = url.toString();
    conn.apiKey.clear();  // Managed server is loopback-only, no auth
    if (!m_startedConfig.modelAlias.isEmpty())
        conn.modelId = m_startedConfig.modelAlias;
    else if (const ModelProfiles::Role *role = ModelProfiles::roleFor(m_settings.requestProfileIdForRole(connectionRoleName(m_startedRole)), connectionRoleName(m_startedRole)))
        conn.modelId = role->alias;
    if (conn.modelId.isEmpty())
        conn.modelId = QStringLiteral("llocr-local");
    conn.connectionTimeoutMs = m_settings.connectionTimeoutMs();
    conn.responseTimeoutMs = m_settings.responseTimeoutMs();
    return conn;
}

void RuntimeController::fetchManagedModels()
{
    m_modelsBaseUrl = buildManagedConnection().baseUrl;
    if (!m_modelsNet)
        m_modelsNet = new QNetworkAccessManager(this);

    HttpClient::Options options;
    options.timeoutMs = kModelsRequestTimeoutMs;
    QNetworkRequest req = HttpClient::makeRequest(QUrl(m_modelsBaseUrl + QStringLiteral("/v1/models")), options);
    QNetworkReply *reply = m_modelsNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onModelsReply(reply); });
}

void RuntimeController::onModelsReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_resolveInProgress)
        return;

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Failed to query /v1/models: %1").arg(reply->errorString()));
        return;
    }

    QJsonArray data;
    QString parseError;
    if (!LlamaServerProcess::parseModelsResponse(reply->readAll(), data, &parseError)) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server returned a malformed /v1/models response: %1").arg(parseError));
        return;
    }
    if (data.isEmpty()) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server advertised no models via /v1/models. "
                       "Select a model in Settings → Models"));
        return;
    }

    const QString alias = buildManagedConnection().modelId;
    QString modelId;
    for (const QJsonValue &v : data) {
        if (v.toObject().value(QStringLiteral("id")).toString() == alias) {
            modelId = alias;
            break;
        }
    }
    if (modelId.isEmpty())
        modelId = data.first().toObject().value(QStringLiteral("id")).toString();

    ResolvedConnection conn = buildManagedConnection();
    conn.modelId = modelId;
    setBusyState(AppBusyState::Idle);
    completeResolve(std::move(conn));
}

QString RuntimeController::describeServerFailure() const
{
    QString msg = m_server ? translateServerLine(m_server->lastError()) : QString();
    const QString tail = m_server ? m_server->ringBuffer(20).join(QStringLiteral("\n")) : QString();
    if (!tail.isEmpty()) {
        if (msg.isEmpty())
            msg = tr("Server failed");
        msg += QStringLiteral("\n\n") + tail;
    }
    return msg;
}

QString RuntimeController::translateServerLine(const QString &line)
{
    if (line.isEmpty())
        return QString();
    if (line.contains(QStringLiteral("address already in use")) || line.contains(QStringLiteral("failed to bind")) || line.contains(QStringLiteral("cannot bind")))
        return tr("Port is busy. Change the port or enable auto-pick");
    if (line.contains(QStringLiteral("unknown argument")) || line.contains(QStringLiteral("invalid argument")))
        return tr("The server rejected an argument that is not supported by your build");
    if (line.contains(QStringLiteral("failed to load model")) || line.contains(QStringLiteral("no such file")))
        return tr("Model file not found. Re-check the model path in Settings");
    if (line.contains(QStringLiteral("cudaMalloc failed")) || line.contains(QStringLiteral("buffer_type_alloc_buffer")))
        return tr("Not enough VRAM. Lower --n-gpu-layers or --ctx-size");
    if (line.contains(QStringLiteral("cudart64")))
        return tr("CUDA runtime not installed. Install the CUDA archive or pick CPU/Vulkan");
    if (line.contains(QStringLiteral("libvulkan.so.1")))
        return tr("Vulkan is unavailable; pick a different backend");
    if (line.contains(QStringLiteral("unknown model architecture")))
        return tr("This GGUF format is not supported by your llama.cpp build");
    if (line.contains(QStringLiteral("did not answer /health")))
        return tr("Server did not respond in time; see the log below");
    return line;
}

QString RuntimeController::startServer()
{
    return startServer(ConnectionRole::PageOcr);
}

QString RuntimeController::startServer(ConnectionRole role)
{
    auto refuse = [this](const QString &message) {
        setStatusMessage(message);
        return message;
    };

    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return refuse(tr("No server binary selected"));
    const QFileInfo fi(program);
    if (!fi.exists())
        return refuse(tr("File not found: %1").arg(program));
    if (m_lockedOut)
        return refuse(tr("Another instance is already running"));

    if (m_server && (m_server->state() == RuntimeState::Starting || m_server->state() == RuntimeState::Ready || m_server->state() == RuntimeState::Stopping))
        return refuse(tr("Server is already running"));

    if (modeFromSettings(m_settings) == ConnectionMode::Managed) {
        const QString roleError = roleConfigError(role);
        if (!roleError.isEmpty()) {
            setStatusMessage(roleError);
            return roleError;
        }
    }

    RuntimePaths paths = m_installedState ? m_installedState->paths() : currentPaths();
    paths.ensureDirectories();
    const quint64 generation = ++m_startGeneration;
    const QString cacheDir = paths.cacheDir();
    setStatusMessage(tr("Probing %1…").arg(fi.fileName()));

    auto *watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this, [this, watcher, role, program, generation]() {
        const ProbeResult probe = watcher->result();
        watcher->deleteLater();
        if (generation != m_startGeneration)
            return;  // cancelled or superseded by a newer start
        finishStartServer(role, program, probe);
    });
    watcher->setFuture(QtConcurrent::run([program, cacheDir]() { return RuntimeLocator::probeCached(program, cacheDir, kProbeTimeoutMs); }));
    return QString();
}

void RuntimeController::finishStartServer(ConnectionRole role, const QString &program, const ProbeResult &probe)
{
    if (!probe.ok) {
        setStatusMessage(probe.error);
        if (m_resolveInProgress) {
            setBusyState(AppBusyState::Idle);
            failResolve(probe.error);
        }
        return;
    }

    const QFileInfo fi(program);
    RuntimePaths paths = m_installedState ? m_installedState->paths() : currentPaths();
    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings, m_launchProfiles, role);
    cfg.program = program;
    QStringList args = cfg.toArguments(probe.capabilities);

    LlamaServerProcess::Options opts;
    opts.program = program;
    opts.arguments = args;
    opts.workingDirectory = fi.absolutePath();
    opts.host = m_settings.launchHost();
    opts.port = m_settings.launchPort();
    opts.startupTimeoutMs = m_settings.startupTimeoutMs();
    opts.autoRestart = m_settings.autoRestart();
    opts.logFile = paths.serverLogPath();
    opts.ownerJsonPath = QDir(paths.runtimeDir()).filePath(QStringLiteral("owner.json"));
    opts.stopOnExit = m_settings.stopOnExit();

    if (!m_server) {
        m_server = new LlamaServerProcess(opts, this);
        connect(m_server, &LlamaServerProcess::stateChanged, this, [this]() {
            setState(m_server->state());
            onServerStateForResolve();
        });
        connect(m_server, &LlamaServerProcess::statusMessageChanged, this, [this]() { setStatusMessage(m_server->statusMessage()); });
        connect(m_server, &LlamaServerProcess::loadProgressChanged, this, [this]() { setLoadProgressPercent(m_server->loadProgressPercent()); });
    } else {
        m_server->setOptions(opts);
    }

    if (m_logTarget)
        m_logTarget->setServer(m_server);

    setBusyState(AppBusyState::StartingRuntime);
    setLoadProgressPercent(-1);
    const QString err = m_server->start();
    if (!err.isEmpty()) {
        setBusyState(AppBusyState::Idle);
        setState(RuntimeState::Failed);
        setStatusMessage(err);
        if (m_resolveInProgress)
            failResolve(err);
        return;
    }
    if (m_server->state() == RuntimeState::Failed) {
        setBusyState(AppBusyState::Idle);
        const QString fail = describeServerFailure();
        setStatusMessage(fail);
        if (m_resolveInProgress)
            failResolve(fail);
        return;
    }
    setState(RuntimeState::Starting);
    m_startedConfig = cfg;
    m_startedConfig.program = program;
    m_startedRole = role;
    m_hasStartedConfig = true;
    recomputeLaunchConfigDirty();
    setStatusMessage(tr("Starting server…"));
}

void RuntimeController::stopServer()
{
    if (!m_server)
        return;
    cancelPendingRestart();
    setBusyState(AppBusyState::StoppingRuntime);
    m_server->stop();
    setLoadProgressPercent(-1);
    setBusyState(AppBusyState::Idle);
}

void RuntimeController::cancelPendingRestart()
{
    if (m_restartConn) {
        disconnect(m_restartConn);
        m_restartConn = QMetaObject::Connection();
    }
}

void RuntimeController::restartServer()
{
    // Restart brings back the server the role it was serving: the banner's
    // dirty comparison is per started role, so a PageOcr restart here would
    // silently move a block-recognition server onto the page model.
    const ConnectionRole role = m_hasStartedConfig ? m_startedRole : ConnectionRole::PageOcr;
    if (m_server && m_server->state() != RuntimeState::Stopped) {
        setBusyState(AppBusyState::StoppingRuntime);
        setStatusMessage(tr("Stopping…"));
        cancelPendingRestart();
        m_restartConn = connect(m_server, &LlamaServerProcess::stateChanged, this, [this, role]() {
            if (m_server->state() != RuntimeState::Stopped)
                return;
            cancelPendingRestart();
            setBusyState(AppBusyState::Idle);
            const QString err = startServer(role);
            if (!err.isEmpty())
                setStatusMessage(err);
        });
        m_server->stop();
        return;
    }
    const QString err = startServer(role);
    if (!err.isEmpty())
        setStatusMessage(err);
}

QString RuntimeController::launchCommandPreview()
{
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return QString();
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return QString();
    const RuntimePaths paths = m_installedState ? m_installedState->paths() : currentPaths();
    ProbeResult probe;
    RuntimeLocator::cachedProbe(program, paths.cacheDir(), probe);
    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings, m_launchProfiles);
    cfg.program = program;
    return cfg.toDisplayCommand(probe.ok ? probe.capabilities : ServerCapabilities{});
}

QVariantMap RuntimeController::estimateModelMemory(const QString &modelPath, const QString &role)
{
    QVariantMap out;
    const QString modelId = m_settings.requestProfileIdForRole(role);
    const LaunchProfile launch = m_launchProfiles.activeProfile(modelId, role);
    const QString mmprojPath = m_settings.launchMmprojPathForRole(role).trimmed();

    // A model of the catalog carries the numbers measured against llama.cpp
    // (ADR 191); anything else is read from its own header.
    const ModelProfiles::Profile *profile = ModelProfiles::findBuiltIn(modelId);
    ModelMemoryShape shape;
    bool valid = false;
    QString error;
    if (profile && profile->memory.valid) {
        shape = shapeFromProfile(profile->memory);
        valid = true;
    } else {
        shape = shapeFromFile(modelPath, &valid, &error);
    }
    shape.weightsBytes = QFileInfo(modelPath).size();
    shape.projectorBytes = mmprojPath.isEmpty() ? 0 : QFileInfo(mmprojPath).size();

    const int visionTokens = profile ? ModelProfiles::visionTokensFor(*profile, role) : 0;
    const ModelMemoryBudget budget = budgetFromLaunch(launch, visionTokens);
    const ModelMemoryEstimate e = estimateMemory(shape, budget);

    out.insert(QStringLiteral("modelBytes"), e.modelBytes);
    out.insert(QStringLiteral("mmprojBytes"), e.mmprojBytes);
    out.insert(QStringLiteral("draftBytes"), e.draftBytes);
    out.insert(QStringLiteral("draftContextBytes"), e.draftContextBytes);
    out.insert(QStringLiteral("kvCacheBytes"), e.kvCacheBytes);
    out.insert(QStringLiteral("computeBufferBytes"), e.computeBufferBytes);
    out.insert(QStringLiteral("visionBufferBytes"), e.visionBufferBytes);
    out.insert(QStringLiteral("overheadBytes"), e.overheadBytes);
    out.insert(QStringLiteral("totalBytes"), e.totalBytes);
    out.insert(QStringLiteral("requiredVramBytes"), e.requiredVramBytes);
    out.insert(QStringLiteral("systemRamBytes"), e.systemRamBytes);
    out.insert(QStringLiteral("valid"), valid);
    out.insert(QStringLiteral("error"), error);
    out.insert(QStringLiteral("nLayer"), e.nLayer);
    out.insert(QStringLiteral("nKvLayer"), e.nKvLayer);
    out.insert(QStringLiteral("nKvHead"), e.nKvHead);
    out.insert(QStringLiteral("headDim"), e.headDim);
    out.insert(QStringLiteral("overBudget"), valid && e.systemRamBytes > 0 && e.requiredVramBytes > static_cast<qint64>(e.systemRamBytes * kMemoryBudgetFactor));

    const HardwareReport hardware = HardwareInfo::detect();
    out.insert(QStringLiteral("vramBytes"), hardware.primaryGpuMemoryBytes());
    out.insert(QStringLiteral("vramBudgetBytes"), hardware.memoryBudgetBytes());
    out.insert(QStringLiteral("overVramBudget"), valid && hardware.primaryGpuMemoryBytes() > 0 && e.requiredVramBytes > hardware.primaryGpuMemoryBytes());
    out.insert(QStringLiteral("totalLabel"), formatBytes(e.totalBytes));
    out.insert(QStringLiteral("requiredLabel"), formatBytes(e.requiredVramBytes));
    out.insert(QStringLiteral("draftLabel"), formatBytes(e.draftBytes + e.draftContextBytes));
    out.insert(QStringLiteral("modelLabel"), formatBytes(e.modelBytes));
    out.insert(QStringLiteral("mmprojLabel"), formatBytes(e.mmprojBytes));
    out.insert(QStringLiteral("kvLabel"), formatBytes(e.kvCacheBytes));
    out.insert(QStringLiteral("computeLabel"), formatBytes(e.computeBufferBytes));
    out.insert(QStringLiteral("visionLabel"), formatBytes(e.visionBufferBytes));
    out.insert(QStringLiteral("overheadLabel"), formatBytes(e.overheadBytes));
    out.insert(QStringLiteral("vramLabel"), formatBytes(hardware.primaryGpuMemoryBytes()));
    return out;
}

void RuntimeController::cancelPendingStart()
{
    if (modeFromSettings(m_settings) != ConnectionMode::Managed)
        return;
    if (!m_resolveInProgress && m_deferredResolves.empty())
        return;

    setBusyState(AppBusyState::Idle);
    ++m_startGeneration;
    const QString message = tr("Server start cancelled");

    const auto deferred = std::move(m_deferredResolves);
    m_deferredResolves.clear();

    if (m_resolveInProgress) {
        failResolve(message);
        if (m_server && m_server->state() == RuntimeState::Starting) {
            m_server->stop();
            setLoadProgressPercent(-1);
        }
    }

    ResolvedConnection fail;
    fail.error = message;
    deliverCallbacks(deferred, fail);
    setStatusMessage(tr("Stopped"));
}

void RuntimeController::shutdownSync()
{
    cancelPendingRestart();
    if (m_server)
        m_server->shutdownSync(kShutdownTimeoutMs);
}

void RuntimeController::retranslate()
{
    if (m_server)
        m_server->retranslate();
}

}  // namespace llocr