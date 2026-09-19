#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

#include "app/SettingsStore.h"
#include "app/LaunchProfileStore.h"
#include "core/LaunchProfile.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimeLog.h"
#include "runtime/RuntimePaths.h"
#include "runtime/ServerCapabilities.h"
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
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_launchProfiles(launchProfiles)
{
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

QString RuntimeController::configNotReadyMessage() const
{
    const QString program = m_settings.serverPath().trimmed();
    if (program.isEmpty())
        return tr("Managed server is not configured");
    if (!QFileInfo(program).isFile())
        return tr("File not found: %1").arg(program);

    const QString model = m_settings.launchModelPath().trimmed();
    if (model.isEmpty())
        return tr("Model is not selected — pick a model in Settings → Models "
                  "or in the Setup wizard");
    return tr("Model file not found: %1 — re-select the model in Settings → Models")
               .arg(model);
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
    ensureConnectionReady(nullptr, onResolved);
}

void RuntimeController::ensureConnectionReady(
    QObject *context, const std::function<void(const ResolvedConnection &)> &onResolved)
{
    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        onResolved(resolveExternal());
        return;
    }

    if (m_resolveInProgress) {
        m_resolveCallbacks.push_back({context, context != nullptr, onResolved});
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

    const bool serverLive = m_state == RuntimeState::Ready
                         || m_state == RuntimeState::Starting;
    if (!m_configValid && !serverLive) {
        failNow(configNotReadyMessage());
        return;
    }

    if (m_state != RuntimeState::Ready && m_state != RuntimeState::Starting
        && m_state != RuntimeState::Stopping) {
        if (!m_settings.autoStart() && !m_settings.startOnDemand()) {
            failNow(tr("Server is not set to start automatically. "
                       "Start it from the main window or Settings → Runtime."));
            return;
        }
    }

    m_resolveInProgress = true;
    m_resolveCallbacks.push_back({context, context != nullptr, onResolved});
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
    case RuntimeState::NotConfigured:
        if (!m_configValid) {
            failResolve(configNotReadyMessage());
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
    for (const auto &cb : callbacks) {
        if (cb.guarded && cb.context.isNull())
            continue;
        cb.onResolved(conn);
    }
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
        if (m_state == RuntimeState::Ready || m_state == RuntimeState::Stopped
            || m_state == RuntimeState::Failed)
            setBusyState(AppBusyState::Idle);
        return;
    }
    if (m_state == RuntimeState::Ready) {
        fetchManagedModels();
    } else if (m_state == RuntimeState::Failed) {
        setBusyState(AppBusyState::Idle);
        failResolve(describeServerFailure());
    } else if (m_state == RuntimeState::Stopping
               || m_state == RuntimeState::Stopped) {
        setBusyState(AppBusyState::Idle);
        failResolve(tr("Server stopped"));
    }
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
        const QString model = m_settings.launchModelPath().trimmed();
        if (model.isEmpty() || !QFileInfo(model).isFile()) {
            const QString msg = model.isEmpty()
                ? tr("Model is not selected — pick a model in Settings → Models "
                     "or in the Setup wizard")
                : tr("Model file not found: %1 — re-select the model in "
                     "Settings → Models").arg(model);
            setStatusMessage(msg);
            return msg;
        }
    }

    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    const ProbeResult probe =
        RuntimeLocator::probeCached(program, paths.cacheDir(), kProbeTimeoutMs);
    if (!probe.ok) {
        setStatusMessage(probe.error);
        return probe.error;
    }

    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings,
                                                              m_launchProfiles);
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
    setLoadProgressPercent(-1);   // §H.7: no stale percent across starts
    const QString err = m_server->start();
    if (!err.isEmpty()) {
        setBusyState(AppBusyState::Idle);
        setState(RuntimeState::Failed);
        setStatusMessage(err);
        return err;
    }
    if (m_server->state() == RuntimeState::Failed) {
        setBusyState(AppBusyState::Idle);
        const QString fail = describeServerFailure();
        setStatusMessage(fail);
        return fail;
    }
    setState(RuntimeState::Starting);
    setStatusMessage(QObject::tr("Starting server…"));
    return QString();
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
        setStatusMessage(QObject::tr("Stopping…"));
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
    const ProbeResult r = RuntimeLocator::probe(path, kProbeTimeoutMs);
    const QString summary = RuntimeLocator::probeSummary(r);
    setStatusMessage(summary);
    return summary;
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
    if (!m_resolveInProgress)
        return;

    setBusyState(AppBusyState::Idle);
    failResolve(tr("Server start cancelled"));
    if (m_server && m_server->state() == RuntimeState::Starting) {
        m_server->stop();
        setLoadProgressPercent(-1);
    }
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