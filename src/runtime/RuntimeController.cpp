#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

#include "app/SettingsStore.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimeLog.h"
#include "runtime/RuntimePaths.h"
#include "runtime/ServerLaunchConfig.h"
#include "runtime/SingleInstanceGuard.h"

namespace llocr {

namespace {
// Named timeouts/limits for the managed-runtime network/probe/shutdown paths
// (review 2.7). The probe budget is generous: on macOS the very first exec of
// a llama.cpp build past a cold `com.apple.metal` shader cache takes tens of
// seconds (compiling ~20-30 MB of kernel libraries, ADR 53); once built the
// probe answers in ~50 ms, so the budget only matters on a cold cache or a
// genuinely frozen binary.
constexpr int kModelsRequestTimeoutMs = 10000;  // /v1/models query
constexpr int kProbeTimeoutMs = 120000;         // RuntimeLocator::probeCached (cold Metal cache)
constexpr int kShutdownTimeoutMs = 5000;         // shutdownSync grace
}  // namespace

RuntimeController::RuntimeController(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // §1.4 configValid = a valid server binary is selected AND (in Managed) the
    // model file exists. Recompute it whenever any input changes.
    recomputeConfigValid();
    connect(&m_settings, &SettingsStore::serverPathChanged, this,
            &RuntimeController::recomputeConfigValid);
    connect(&m_settings, &SettingsStore::connectionModeChanged, this,
            &RuntimeController::recomputeConfigValid);
    connect(&m_settings, &SettingsStore::launchModelPathChanged, this,
            &RuntimeController::recomputeConfigValid);
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
    // A server binary must be selected and exist on disk. In Managed the model
    // file must also exist; External mode depends on neither.
    // §3.8: configValid deliberately means only "files exist" — it does NOT
    // imply the locator probe passed. The probe (capabilities, version) runs
    // later, at start time (probeCached in startServer()). This is a known,
    // accepted deviation from the stricter §1.4 wording of `canRecognize`.
    bool valid = !m_settings.serverPath().trimmed().isEmpty();
    if (valid && !QFileInfo(m_settings.serverPath()).isFile())
        valid = false;

    if (modeFromSettings(m_settings) == ConnectionMode::Managed) {
        const QString model = m_settings.launchModelPath().trimmed();
        if (model.isEmpty() || !QFileInfo(model).isFile())
            valid = false;
    }

    if (valid == m_configValid)
        return;
    m_configValid = valid;
    emit configValidChanged();
}

ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
{
    // Typed facade: the string↔enum mapping lives in SettingsStore (review 2.6).
    return settings.mode();
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

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

ResolvedConnection RuntimeController::resolveExternal() const
{
    ResolvedConnection conn;
    conn.baseUrl = m_settings.baseUrl();
    conn.apiKey = m_settings.apiKey();
    conn.modelId = m_settings.modelName();
    conn.timeoutMs = m_settings.connectionTimeoutMs();
    return conn;
}

void RuntimeController::ensureConnectionReady(
    const std::function<void(const ResolvedConnection &)> &onResolved)
{
    // External resolves synchronously (ADR 26); never transitions through a
    // Starting state.
    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        onResolved(resolveExternal());
        return;
    }

    // Managed: deduplicate — concurrent callers share the in-flight resolve by
    // queuing their callback; completeResolve() invokes every queued callback.
    if (m_resolveInProgress) {
        m_resolveCallbacks.push_back(onResolved);
        return;
    }

    auto failNow = [onResolved](const QString &message) {
        ResolvedConnection fail;
        fail.error = message;
        onResolved(fail);
    };

    if (m_lockedOut) {
        failNow(tr("Another LLocr instance is already running"));
        return;
    }

    if (!m_configValid) {
        failNow(tr("Managed server is not configured"));
        return;
    }

    // Not currently Ready/Starting: only auto-start when the user allowed it.
    if (m_state != RuntimeState::Ready && m_state != RuntimeState::Starting
        && m_state != RuntimeState::Stopping) {
        if (!m_settings.autoStart() && !m_settings.startOnDemand()) {
            failNow(tr("Server is not set to start automatically. "
                       "Start it from Settings → Runtime."));
            return;
        }
    }

    m_resolveInProgress = true;
    m_resolveCallbacks.push_back(onResolved);
    beginManagedResolve();
}

// --- Managed resolve machinery ---------------------------------------------

void RuntimeController::beginManagedResolve()
{
    switch (m_state) {
    case RuntimeState::Ready:
        // Still verify the alias via /v1/models (§4.2) instead of trusting
        // settings blindly.
        fetchManagedModels();
        break;
    case RuntimeState::Starting:
    case RuntimeState::Stopping:
        // Already going (e.g. started from Settings → Runtime); the
        // stateChanged handler drives the resolve to completion.
        break;
    case RuntimeState::NotConfigured:
        if (!m_configValid) {
            failResolve(tr("Managed server is not configured"));
            break;
        }
        Q_FALLTHROUGH();
    case RuntimeState::Stopped:
    case RuntimeState::Failed: {
        setBusyState(AppBusyState::StartingRuntime);
        const QString err = startServer();
        if (!err.isEmpty()) {
            setBusyState(AppBusyState::Idle);
            failResolve(err);
        }
        break;
    }
    }  // switch (m_state)
}

void RuntimeController::completeResolve(ResolvedConnection conn)
{
    if (!m_resolveInProgress)
        return;
    m_resolveInProgress = false;
    const auto callbacks = std::move(m_resolveCallbacks);
    m_resolveCallbacks.clear();
    for (const auto &cb : callbacks)
        cb(conn);
}

void RuntimeController::failResolve(const QString &message)
{
    ResolvedConnection fail;
    fail.error = message;
    completeResolve(std::move(fail));
}

void RuntimeController::onServerStateForResolve()
{
    if (!m_resolveInProgress)
        return;
    if (m_state == RuntimeState::Ready) {
        fetchManagedModels();
    } else if (m_state == RuntimeState::Failed) {
        setBusyState(AppBusyState::Idle);
        failResolve(describeServerFailure());
    }
}

ResolvedConnection RuntimeController::buildManagedConnection() const
{
    ResolvedConnection conn;
    const int port = m_server ? m_server->resolvedPort() : m_settings.launchPort();
    // §3.5: assemble via QUrl so an IPv6 host (::1) is bracketed correctly.
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

    QNetworkRequest req(QUrl(m_modelsBaseUrl + QStringLiteral("/v1/models")));
    req.setTransferTimeout(kModelsRequestTimeoutMs);
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

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server advertised no models via /v1/models"));
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
        modelId = data.first().toObject().value(QStringLiteral("id")).toString();  // §4.2 fallback

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
    // §7.5 error matrix → human-readable message; unknown lines pass through so
    // the caller can still surface the raw tail.
    if (line.isEmpty())
        return QString();
    if (line.contains(QStringLiteral("address already in use"))
        || line.contains(QStringLiteral("failed to bind"))
        || line.contains(QStringLiteral("cannot bind")))
        return QObject::tr("Port is busy. Change the port or enable auto-pick");
    if (line.contains(QStringLiteral("unknown argument"))
        || line.contains(QStringLiteral("invalid argument")))
        return QObject::tr("The server rejected an argument that is not supported by your build");
    if (line.contains(QStringLiteral("failed to load model"))
        || line.contains(QStringLiteral("no such file")))
        return QObject::tr("Model file not found. Re-check the model path in Settings");
    if (line.contains(QStringLiteral("cudaMalloc failed"))
        || line.contains(QStringLiteral("buffer_type_alloc_buffer")))
        return QObject::tr("Not enough VRAM. Lower --n-gpu-layers or --ctx-size");
    if (line.contains(QStringLiteral("cudart64")))
        return QObject::tr("CUDA runtime not installed. Install the CUDA archive or pick CPU/Vulkan");
    if (line.contains(QStringLiteral("libvulkan.so.1")))
        return QObject::tr("Vulkan is unavailable; pick a different backend");
    if (line.contains(QStringLiteral("unknown model architecture")))
        return QObject::tr("This GGUF format is not supported by your llama.cpp build");
    if (line.contains(QStringLiteral("did not answer /health")))
        return QObject::tr("Server did not respond in time; see the log below");
    return line;
}

// ---------------------------------------------------------------------------
// Stage B: managed server lifecycle (Runtime settings tab)
// ---------------------------------------------------------------------------

QString RuntimeController::startServer()
{
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return QObject::tr("No server binary selected");
    const QFileInfo fi(program);
    if (!fi.exists())
        return QObject::tr("File not found: %1").arg(program);
    if (m_lockedOut)
        return QObject::tr("Another instance is already running");

    if (m_server && (m_server->state() == RuntimeState::Starting
                     || m_server->state() == RuntimeState::Ready
                     || m_server->state() == RuntimeState::Stopping))
        return QObject::tr("Server is already running");

    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    const ProbeResult probe =
        RuntimeLocator::probeCached(program, paths.cacheDir(), kProbeTimeoutMs);
    if (!probe.ok) {
        setStatusMessage(probe.error);
        return probe.error;
    }

    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings);
    cfg.program = program;
    // toArguments() emits --host/--port for the configured port; LlamaServerProcess
    // only fills a --port when the argv has none (auto-pick, port 0) — so the
    // port has a single source and no duplicate flag is produced (review 2.5).
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
    // The dedicated log view follows the live server (§ review 3.4); it is set
    // once per (re)spawn so restarts keep the window attached.
    if (m_logTarget)
        m_logTarget->setServer(m_server);

    setBusyState(AppBusyState::StartingRuntime);
    setLoadProgressPercent(-1);   // §H.7: no stale percent across starts
    const QString err = m_server->start();
    if (!err.isEmpty()) {
        setBusyState(AppBusyState::Idle);
        setState(RuntimeState::Failed);
        setStatusMessage(err);
        return err;
    }
    setState(RuntimeState::Starting);
    setStatusMessage(QObject::tr("Starting server…"));
    return QString();
}

void RuntimeController::stopServer()
{
    if (!m_server)
        return;
    setBusyState(AppBusyState::StoppingRuntime);
    // §2.7: stop() is asynchronous now; the Stopped state arrives via the
    // server's stateChanged signal when the child actually exits.
    m_server->stop();
    setLoadProgressPercent(-1);
    setBusyState(AppBusyState::Idle);
}

void RuntimeController::restartServer()
{
    // §2.7: stop() is asynchronous, so a fresh start cannot run in the same
    // tick. Terminate now; start once the server reports Stopped.
    if (m_server && m_server->state() != RuntimeState::Stopped) {
        setBusyState(AppBusyState::StoppingRuntime);
        setStatusMessage(QObject::tr("Stopping…"));
        QMetaObject::Connection restartConn;
        restartConn = connect(
            m_server, &LlamaServerProcess::stateChanged, this,
            [this, restartConn]() {
                if (m_server->state() != RuntimeState::Stopped)
                    return;
                disconnect(restartConn);
                setBusyState(AppBusyState::Idle);
                const QString err = startServer();
                if (!err.isEmpty())
                    setStatusMessage(err);
            },
            Qt::SingleShotConnection);
        m_server->stop();
        return;
    }
    const QString err = startServer();
    if (!err.isEmpty())
        setStatusMessage(err);
}

QString RuntimeController::probeRuntimePath(const QString &path)
{
    // Long budget: the first exec after a cold Metal-cache can block for tens
    // of seconds (ADR 53); once warmed the probe is ~50 ms.
    const ProbeResult r = RuntimeLocator::probe(path, kProbeTimeoutMs);
    const QString summary = RuntimeLocator::probeSummary(r);
    setStatusMessage(summary);
    return summary;
}

QString RuntimeController::autoDiscoverPath()
{
    // Long probe budget: cold Metal shader cache can delay the first exec (ADR 53).
    const QString found = RuntimeLocator::autoDiscover(kProbeTimeoutMs);
    if (!found.isEmpty())
        setStatusMessage(RuntimeLocator::probeSummary(RuntimeLocator::probe(found, kProbeTimeoutMs)));
    else
        setStatusMessage(QObject::tr("No llama-server binary found automatically"));
    return found;
}

QString RuntimeController::launchCommandPreview()
{
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return QString();
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return QString();
    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    const ProbeResult probe =
        RuntimeLocator::probeCached(program, paths.cacheDir(), kProbeTimeoutMs);
    if (!probe.ok)
        return QString();
    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings);
    cfg.program = program;
    return cfg.toDisplayCommand(probe.capabilities);
}

QVariantMap RuntimeController::estimateModelMemory(const QString &modelPath,
                                                   int ctxSize)
{
    QVariantMap out;
    const ModelMemoryEstimate e =
        ::llocr::estimateModelMemory(modelPath, ctxSize, m_settings.launchCacheTypeK(),
                                     m_settings.launchCacheTypeV());
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
    if (!m_resolveInProgress)
        return;

    // Interrupt a still-starting server (a Ready server is left running for
    // reuse). The recognition flow drops out via the resolve error below.
    // §2.7: stop() is asynchronous; the Stopped state arrives via the server's
    // stateChanged signal, so only reset the resolve machinery here.
    if (m_server && m_server->state() == RuntimeState::Starting) {
        m_server->stop();
        setLoadProgressPercent(-1);
    }
    // Reset unconditionally: in the /v1/models window (server Ready) or while
    // Stopping, busyState is still StartingRuntime and must not stick.
    setBusyState(AppBusyState::Idle);
    failResolve(tr("Server start cancelled"));
    setStatusMessage(tr("Stopped"));
}

void RuntimeController::shutdownSync()
{
    if (m_server)
        m_server->shutdownSync(kShutdownTimeoutMs);
}

}  // namespace llocr