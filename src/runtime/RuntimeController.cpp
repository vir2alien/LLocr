#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

#include "app/SettingsStore.h"
#include "app/LaunchProfileStore.h"
#include "core/LaunchProfile.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimeLog.h"
#include "runtime/RuntimePaths.h"
#include "runtime/HttpClient.h"
#include "runtime/ProcessGuard.h"
#include "runtime/ServerCapabilities.h"
#include "runtime/ServerOwner.h"
#include "runtime/ServerLaunchConfig.h"
#include "runtime/SingleInstanceGuard.h"

namespace llocr {

namespace {
constexpr int kModelsRequestTimeoutMs = 10000;  // /v1/models query
constexpr int kProbeTimeoutMs = 120000;         // RuntimeLocator::probeCached (cold Metal cache)
constexpr int kShutdownTimeoutMs = 5000;         // shutdownSync grace
}  // namespace

RuntimeController::RuntimeController(SettingsStore &settings,
                                     LaunchProfileStore &launchProfiles,
                                     LaunchProfileStore *checkLaunchProfiles,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_launchProfiles(launchProfiles)
    , m_checkLaunchProfiles(checkLaunchProfiles ? checkLaunchProfiles : &launchProfiles)
{
    if (!checkLaunchProfiles) {
        qWarning("RuntimeController: no check launch profile store wired - "
                 "the check role falls back to the OCR launch profiles");
    }
    recomputeConfigValid();
    connect(&m_settings, &SettingsStore::serverPathChanged, this,
            &RuntimeController::recomputeConfigValid);
    connect(&m_settings, &SettingsStore::connectionModeChanged, this,
            &RuntimeController::recomputeConfigValid);
    connect(&m_settings, &SettingsStore::launchModelPathChanged, this,
            &RuntimeController::recomputeConfigValid);
    connect(&m_settings, &SettingsStore::runtimeRootDirChanged, this,
            &RuntimeController::scanForOrphanedServer);
    connect(&m_settings, &SettingsStore::runtimeModelsDirChanged, this,
            &RuntimeController::scanForOrphanedServer);
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
    // Always re-read: the scan is a small file read plus a liveness probe, and
    // the record can appear or expire between two calls (the user may also have
    // killed the process). The signal fires only when the verdict changes.
    const RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
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
    // Re-validate: the pid may have exited or been recycled since the scan.
    if (!ServerOwner::isOrphan(m_orphan))
        return tr("The orphaned server is already gone");
    if (!ProcessGuard::terminateProcess(m_orphan.pid))
        return tr("Unable to terminate the process (pid %1)").arg(m_orphan.pid);
    ServerOwner::clear(m_orphanJsonPath);
    m_orphan = ServerOwnerRecord{};
    emit orphanChanged();
    return {};
}

void RuntimeController::setSingleInstanceHeld(bool held)
{
    if (m_lockedOut == held)
        return;
    m_lockedOut = held;
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
    bool valid = !m_settings.serverPath().trimmed().isEmpty();
    if (valid && !QFileInfo(m_settings.serverPath()).isFile())
        valid = false;

    if (modeFromSettings(m_settings) == ConnectionMode::Managed) {
        const QString model = m_settings.launchModelPath().trimmed();
        if (model.isEmpty() || !QFileInfo(model).isFile())
            valid = false;
    }

    if (valid != m_configValid) {
        m_configValid = valid;
        emit configValidChanged();
    }
    if (valid && m_state == RuntimeState::NotConfigured)
        setState(RuntimeState::Stopped);
    else if (!valid && m_state == RuntimeState::Stopped)
        setState(RuntimeState::NotConfigured);
}

ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
{
    return settings.mode();
}

QString RuntimeController::roleModelPath(ConnectionRole role) const
{
    return role == ConnectionRole::Check
               ? m_settings.checkLaunchModelPath().trimmed()
               : m_settings.launchModelPath().trimmed();
}

QString RuntimeController::roleMmprojPath(ConnectionRole role) const
{
    return role == ConnectionRole::Check
               ? m_settings.checkLaunchMmprojPath().trimmed()
               : m_settings.launchMmprojPath().trimmed();
}

bool RuntimeController::serverRunsRole(ConnectionRole role) const
{
    const QString want = roleModelPath(role);
    if (want.isEmpty())
        return true;
    return m_startedModelPath == want && m_startedMmprojPath == roleMmprojPath(role);
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
    if (role == ConnectionRole::Check) {
        if (model.isEmpty())
            return tr("Check model is not selected — pick a model in Settings → Check model");
        if (!QFileInfo(model).isFile())
            return tr("Check model file not found: %1 — re-select the model in "
                      "Settings → Check model").arg(model);
        return QString();
    }
    if (model.isEmpty())
        return tr("Model is not selected — pick a model in Settings → Models "
                  "or in the Setup wizard");
    if (!QFileInfo(model).isFile())
        return tr("Model file not found: %1 — re-select the model in Settings → Models")
                   .arg(model);
    return QString();
}

bool RuntimeController::canRecognize(bool documentLoaded) const
{
    if (!documentLoaded)
        return false;
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return true;
    if (m_state == RuntimeState::Ready)
        return true;
    return m_state == RuntimeState::Stopped && m_configValid
           && (m_settings.autoStart() || m_settings.startOnDemand());
}

ResolvedConnection RuntimeController::resolveExternal(ConnectionRole role) const
{
    ResolvedConnection conn;
    conn.baseUrl = m_settings.baseUrl();
    conn.apiKey = m_settings.apiKey();
    conn.modelId = role == ConnectionRole::Check
                       ? m_settings.checkModelName()
                       : m_settings.modelName();
    conn.timeoutMs = m_settings.connectionTimeoutMs();
    return conn;
}

void RuntimeController::ensureConnectionReady(
    const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(nullptr, ConnectionRole::Ocr, onResolved);
}

void RuntimeController::ensureConnectionReady(
    QObject *context, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(context, ConnectionRole::Ocr, onResolved);
}

void RuntimeController::ensureConnectionReady(
    ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    ensureConnectionReady(nullptr, role, onResolved);
}

void RuntimeController::ensureConnectionReady(
    QObject *context, ConnectionRole role,
    const std::function<void(const ResolvedConnection &)> &onResolved)
{
    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        onResolved(resolveExternal(role));
        return;
    }

    PendingResolve pending;
    pending.context = context;
    pending.guarded = context != nullptr;
    pending.role = role;
    pending.onResolved = onResolved;

    if (m_resolveInProgress) {
        // The in-flight resolve produces a connection for m_resolveRole only.
        auto &queue = role == m_resolveRole ? m_resolveCallbacks : m_deferredResolves;
        queue.push_back(std::move(pending));
        return;
    }

    m_resolveCallbacks.push_back(std::move(pending));
    startResolveForRole(role);
}

// Drives the resolve for the batch currently held in m_resolveCallbacks.
void RuntimeController::startResolveForRole(ConnectionRole role)
{
    m_resolveInProgress = true;
    m_resolveRole = role;

    if (m_lockedOut) {
        failResolve(tr("Another LLocr instance is already running"));
        return;
    }

    const bool serverLive = m_state == RuntimeState::Ready
                         || m_state == RuntimeState::Starting;
    const bool switchNeeded = serverLive && !serverRunsRole(role);

    if (!serverLive || switchNeeded) {
        const QString roleError = roleConfigError(role);
        if (!roleError.isEmpty()) {
            failResolve(roleError);
            return;
        }
        if (!serverLive && m_state != RuntimeState::Stopping
            && !m_settings.autoStart() && !m_settings.startOnDemand()) {
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

void RuntimeController::deliverCallbacks(const std::vector<PendingResolve> &callbacks,
                                         const ResolvedConnection &conn)
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
    const auto callbacks = std::move(m_resolveCallbacks);
    m_resolveCallbacks.clear();
    deliverCallbacks(callbacks, conn);

    // A callback may itself have asked for a connection; that resolve is now in
    // flight and must not be clobbered. The queued other-role requests then
    // simply wait for its completion.
    if (m_resolveInProgress || m_deferredResolves.empty())
        return;

    // Promote the oldest deferred batch (one role) and give it its own dispatch.
    const ConnectionRole nextRole = m_deferredResolves.front().role;
    for (const auto &cb : std::as_const(m_deferredResolves)) {
        if (cb.role == nextRole)
            m_resolveCallbacks.push_back(cb);
    }
    m_deferredResolves.erase(
        std::remove_if(m_deferredResolves.begin(), m_deferredResolves.end(),
                       [nextRole](const PendingResolve &cb) { return cb.role == nextRole; }),
        m_deferredResolves.end());

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
        if (m_state == RuntimeState::Ready || m_state == RuntimeState::Stopped
            || m_state == RuntimeState::Failed)
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
    } else if (m_state == RuntimeState::Stopping
               || m_state == RuntimeState::Stopped) {
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
    setStatusMessage(m_resolveRole == ConnectionRole::Check
                         ? tr("Switching to the check model…")
                         : tr("Switching to the OCR model…"));
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
    conn.modelId = m_settings.launchModelAlias().isEmpty()
                       ? QStringLiteral("llocr-local")
                       : m_settings.launchModelAlias();
    conn.timeoutMs = m_settings.connectionTimeoutMs();
    return conn;
}

void RuntimeController::fetchManagedModels()
{
    m_modelsBaseUrl = buildManagedConnection().baseUrl;
    if (!m_modelsNet)
        m_modelsNet = new QNetworkAccessManager(this);

    // Shared HTTP policy (ADR 108): transfer timeout, manual redirects, and the
    // system proxy applied from the request itself.
    HttpClient::Options options;
    options.timeoutMs = kModelsRequestTimeoutMs;
    QNetworkRequest req = HttpClient::makeRequest(
        QUrl(m_modelsBaseUrl + QStringLiteral("/v1/models")), options);
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

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server returned a malformed /v1/models response: %1")
                        .arg(perr.errorString()));
        return;
    }
    const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
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
    const QString tail = m_server
        ? m_server->ringBuffer(20).join(QStringLiteral("\n"))
        : QString();
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
    if (line.contains(QStringLiteral("address already in use"))
        || line.contains(QStringLiteral("failed to bind"))
        || line.contains(QStringLiteral("cannot bind")))
        return tr("Port is busy. Change the port or enable auto-pick");
    if (line.contains(QStringLiteral("unknown argument"))
        || line.contains(QStringLiteral("invalid argument")))
        return tr("The server rejected an argument that is not supported by your build");
    if (line.contains(QStringLiteral("failed to load model"))
        || line.contains(QStringLiteral("no such file")))
        return tr("Model file not found. Re-check the model path in Settings");
    if (line.contains(QStringLiteral("cudaMalloc failed"))
        || line.contains(QStringLiteral("buffer_type_alloc_buffer")))
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
    return startServer(ConnectionRole::Ocr);
}

QString RuntimeController::startServer(ConnectionRole role)
{
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return tr("No server binary selected");
    const QFileInfo fi(program);
    if (!fi.exists())
        return tr("File not found: %1").arg(program);
    if (m_lockedOut)
        return tr("Another instance is already running");

    if (m_server && (m_server->state() == RuntimeState::Starting
                     || m_server->state() == RuntimeState::Ready
                     || m_server->state() == RuntimeState::Stopping))
        return tr("Server is already running");

    if (modeFromSettings(m_settings) == ConnectionMode::Managed) {
        const QString roleError = roleConfigError(role);
        if (!roleError.isEmpty()) {
            setStatusMessage(roleError);
            return roleError;
        }
    }

    // The probe spawns the binary and waits for it: on a cold cache that is up
    // to two minutes (ADR 105), so it runs on a worker and continues in the
    // callback — the GUI event loop keeps running meanwhile. A generation
    // counter drops the result of a probe that was cancelled or superseded.
    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    paths.ensureDirectories();
    const quint64 generation = ++m_startGeneration;
    const QString cacheDir = paths.cacheDir();
    setStatusMessage(tr("Probing %1…").arg(fi.fileName()));

    auto *watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this,
            [this, watcher, role, program, generation]() {
        const ProbeResult probe = watcher->result();
        watcher->deleteLater();
        if (generation != m_startGeneration)
            return;  // cancelled or superseded by a newer start
        finishStartServer(role, program, probe);
    });
    watcher->setFuture(QtConcurrent::run([program, cacheDir]() {
        return RuntimeLocator::probeCached(program, cacheDir, kProbeTimeoutMs);
    }));
    return QString();
}

void RuntimeController::finishStartServer(ConnectionRole role, const QString &program,
                                          const ProbeResult &probe)
{
    if (!probe.ok) {
        setStatusMessage(probe.error);
        // A resolve that is waiting for this start must fail with it; a start
        // requested from the UI (Start/Restart button) only reports it.
        if (m_resolveInProgress) {
            setBusyState(AppBusyState::Idle);
            failResolve(probe.error);
        }
        return;
    }

    const QFileInfo fi(program);
    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(
        m_settings,
        role == ConnectionRole::Check ? *m_checkLaunchProfiles : m_launchProfiles,
        role);
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
        connect(m_server, &LlamaServerProcess::stateChanged, this,
                [this]() {
                    setState(m_server->state());
                    onServerStateForResolve();
                });
        connect(m_server, &LlamaServerProcess::statusMessageChanged, this,
                [this]() { setStatusMessage(m_server->statusMessage()); });
        connect(m_server, &LlamaServerProcess::loadProgressChanged, this,
                [this]() { setLoadProgressPercent(m_server->loadProgressPercent()); });
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
    m_startedModelPath = cfg.modelPath.trimmed();
    m_startedMmprojPath = cfg.mmprojPath.trimmed();
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
    if (m_server && m_server->state() != RuntimeState::Stopped) {
        setBusyState(AppBusyState::StoppingRuntime);
        setStatusMessage(tr("Stopping…"));
        cancelPendingRestart();
        m_restartConn = connect(
            m_server, &LlamaServerProcess::stateChanged, this,
            [this]() {
                if (m_server->state() != RuntimeState::Stopped)
                    return;
                cancelPendingRestart();
                setBusyState(AppBusyState::Idle);
                const QString err = startServer();
                if (!err.isEmpty())
                    setStatusMessage(err);
            });
        m_server->stop();
        return;
    }
    const QString err = startServer();
    if (!err.isEmpty())
        setStatusMessage(err);
}

QString RuntimeController::probeRuntimePath(const QString &path)
{
    // Same reasoning as startServer(): the probe waits for a child process and
    // must not block the GUI thread. The summary lands in statusMessage when
    // the probe finishes; the QML call sites ignore the return value.
    const QString program = path.trimmed();
    if (program.isEmpty()) {
        const QString empty = QObject::tr("No server binary selected");
        setStatusMessage(empty);
        return empty;
    }
    const quint64 generation = ++m_startGeneration;
    setStatusMessage(tr("Probing %1…").arg(QFileInfo(program).fileName()));
    auto *watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this,
            [this, watcher, program, generation]() {
        const ProbeResult probe = watcher->result();
        watcher->deleteLater();
        if (generation != m_startGeneration)
            return;
        setStatusMessage(RuntimeLocator::probeSummary(probe));
    });
    watcher->setFuture(QtConcurrent::run([program]() {
        return RuntimeLocator::probe(program, kProbeTimeoutMs);
    }));
    return QString();
}


QString RuntimeController::launchCommandPreview()
{
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return QString();
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return QString();
    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    ProbeResult probe;
    RuntimeLocator::cachedProbe(program, paths.cacheDir(), probe);
    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings,
                                                              m_launchProfiles);
    cfg.program = program;
    return cfg.toDisplayCommand(probe.ok ? probe.capabilities
                                         : ServerCapabilities{});
}

QVariantMap RuntimeController::estimateModelMemory(const QString &modelPath)
{
    QVariantMap out;
    const LaunchProfile &profile = m_launchProfiles.activeProfile();
    int ctxSize = 8192;
    QString cacheTypeK;
    QString cacheTypeV;
    if (const LaunchParameter *row = profile.find(QStringLiteral("ctx-size"));
        row && row->kind == LaunchValueKind::Number)
        ctxSize = int(row->value.toDouble());
    if (const LaunchParameter *row = profile.find(QStringLiteral("cache-type-k"));
        row && row->kind == LaunchValueKind::Text)
        cacheTypeK = row->value.toString();
    if (const LaunchParameter *row = profile.find(QStringLiteral("cache-type-v"));
        row && row->kind == LaunchValueKind::Text)
        cacheTypeV = row->value.toString();

    const ModelMemoryEstimate e =
        ::llocr::estimateModelMemory(modelPath, ctxSize, cacheTypeK, cacheTypeV);
    out.insert(QStringLiteral("modelBytes"), e.modelBytes);
    out.insert(QStringLiteral("kvCacheBytes"), e.kvCacheBytes);
    out.insert(QStringLiteral("totalBytes"), e.totalBytes);
    out.insert(QStringLiteral("systemRamBytes"), e.systemRamBytes);
    out.insert(QStringLiteral("valid"), e.valid);
    out.insert(QStringLiteral("error"), e.error);
    out.insert(QStringLiteral("nLayer"), e.nLayer);
    out.insert(QStringLiteral("nKvHead"), e.nKvHead);
    out.insert(QStringLiteral("headDim"), e.headDim);
    return out;
}

void RuntimeController::cancelPendingStart()
{
    if (modeFromSettings(m_settings) != ConnectionMode::Managed)
        return;
    if (!m_resolveInProgress && m_deferredResolves.empty())
        return;

    setBusyState(AppBusyState::Idle);
    // Invalidate an in-flight probe: it must not start the server after a Stop.
    ++m_startGeneration;
    const QString message = tr("Server start cancelled");

    // Drop the queued other-role requests first: a cancel must not let them
    // start (or switch to) the server right after the user pressed Stop.
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