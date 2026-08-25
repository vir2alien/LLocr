#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFuture>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QUrl>
#include <QVariantMap>

#include <memory>

#include "app/SettingsStore.h"
#include "core/ProviderConfig.h"
#include "providers/OpenAiProvider.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/ModelMemoryEstimator.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimePaths.h"
#include "runtime/ServerLaunchConfig.h"

namespace llocr {

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
    return settings.connectionMode() == QString::fromUtf8(SettingsStore::kModeManaged)
               ? ConnectionMode::Managed
               : ConnectionMode::External;
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

QFuture<ResolvedConnection> RuntimeController::makeFuture(ResolvedConnection conn) const
{
    QFutureInterface<ResolvedConnection> promise;
    promise.reportStarted();
    promise.reportResult(std::move(conn));
    promise.reportFinished();
    return promise.future();
}

QFuture<ResolvedConnection> RuntimeController::resolveExternalFuture() const
{
    return makeFuture(resolveExternal());
}

QFuture<ResolvedConnection> RuntimeController::ensureConnectionReady()
{
    // External resolves synchronously (ADR 26); never transitions through a
    // Starting state.
    if (modeFromSettings(m_settings) == ConnectionMode::External)
        return resolveExternalFuture();

    // Managed: deduplicate — concurrent callers share the in-flight resolve.
    if (m_resolveInProgress)
        return m_activeResolve.future();

    if (m_lockedOut)
        return makeFuture(ResolvedConnection{{}, {}, {}, 0,
                                             tr("Another LLocr instance is already running")});

    if (!m_configValid)
        return makeFuture(ResolvedConnection{{}, {}, {}, 0,
                                              tr("Managed server is not configured")});

    // Not currently Ready/Starting: only auto-start when the user allowed it.
    if (m_state != RuntimeState::Ready && m_state != RuntimeState::Starting
        && m_state != RuntimeState::Stopping) {
        if (!m_settings.autoStart() && !m_settings.startOnDemand())
            return makeFuture(ResolvedConnection{
                {}, {}, {}, 0,
                tr("Server is not set to start automatically. Start it from Settings → Runtime.")});
    }

    return beginManagedResolve();
}

// --- Managed resolve machinery ---------------------------------------------

QFuture<ResolvedConnection> RuntimeController::beginManagedResolve()
{
    m_resolveInProgress = true;
    m_activeResolve = QFutureInterface<ResolvedConnection>();
    m_activeResolve.reportStarted();

    switch (m_state) {
    case RuntimeState::Ready:
        completeResolve(buildManagedConnection());
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
    }
    return m_activeResolve.future();
}

void RuntimeController::completeResolve(ResolvedConnection conn)
{
    if (!m_resolveInProgress)
        return;
    m_resolveInProgress = false;
    m_activeResolve.reportResult(std::move(conn));
    m_activeResolve.reportFinished();
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
    conn.baseUrl = QStringLiteral("http://%1:%2").arg(m_settings.launchHost()).arg(port);
    conn.apiKey.clear();  // Managed server is loopback-only, no auth
    conn.modelId = m_settings.launchModelAlias().isEmpty()
                       ? QStringLiteral("llocr-local")
                       : m_settings.launchModelAlias();
    conn.timeoutMs = m_settings.startupTimeoutMs();
    return conn;
}

void RuntimeController::fetchManagedModels()
{
    m_modelsBaseUrl = buildManagedConnection().baseUrl;
    if (!m_modelsNet)
        m_modelsNet = new QNetworkAccessManager(this);

    QNetworkRequest req(QUrl(m_modelsBaseUrl + QStringLiteral("/v1/models")));
    req.setTransferTimeout(10000);
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
    const QString tail = lastLogLines(20);
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

QString RuntimeController::lastLogLines(int count) const
{
    return m_server ? m_server->ringBuffer(count).join(QStringLiteral("\n")) : QString();
}

QImage RuntimeController::makeTestImage()
{
    // Built-in synthetic test image: small white surface with a black bar so a
    // real model has something cheap to describe and the full HTTP round-trip
    // is exercised end-to-end.
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.fillRect(4, 24, 24, 4, Qt::black);
    p.end();
    return img;
}

// ---------------------------------------------------------------------------
// Self-test (§ Stage G-core task 5)
// ---------------------------------------------------------------------------

QFuture<SelfTestResult> RuntimeController::runSelfTest()
{
    auto promise = std::make_shared<QFutureInterface<SelfTestResult>>();
    promise->reportStarted();

    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        SelfTestResult r;
        r.error = tr("Self-test is available only in Managed mode");
        promise->reportResult(std::move(r));
        promise->reportFinished();
        return promise->future();
    }

    // Start (or reuse) the server first, then issue one real request.
    auto *watch = new QFutureWatcher<ResolvedConnection>(this);
    connect(watch, &QFutureWatcher<ResolvedConnection>::finished, this,
            [this, watch, promise]() {
                const ResolvedConnection conn = watch->result();
                watch->deleteLater();
                if (conn.baseUrl.isEmpty()) {
                    SelfTestResult r;
                    r.error = conn.error.isEmpty() ? tr("Server not available") : conn.error;
                    promise->reportResult(std::move(r));
                    promise->reportFinished();
                    return;
                }
                runSelfTestRequest(conn, promise);
            });
    watch->setFuture(ensureConnectionReady());
    return promise->future();
}

void RuntimeController::runSelfTestRequest(
    const ResolvedConnection &conn, std::shared_ptr<QFutureInterface<SelfTestResult>> promise)
{
    if (!m_selftestProvider)
        m_selftestProvider = new OpenAiProvider(this);

    OcrRequest request;
    request.image = makeTestImage();
    request.prompt = tr("Describe the text in this image in one short line.");
    request.modelId = conn.modelId;

    ProviderConfig config;
    config.baseUrl = conn.baseUrl;
    config.apiKey = conn.apiKey;
    config.timeoutMs = conn.timeoutMs;

    QFutureWatcher<OcrResult> *watch = new QFutureWatcher<OcrResult>(this);
    connect(watch, &QFutureWatcher<OcrResult>::finished, this,
            [promise, watch]() {
                const OcrResult res =
                    watch->future().resultCount() > 0 ? watch->result()
                                                      : OcrResult::makeError(QObject::tr("No response"));
                watch->deleteLater();
                SelfTestResult r;
                if (res.success) {
                    r.ok = true;
                    r.text = res.text;
                } else {
                    r.error =
                        res.errorMessage.isEmpty() ? QObject::tr("Recognition failed") : res.errorMessage;
                }
                promise->reportResult(std::move(r));
                promise->reportFinished();
            });
    watch->setFuture(m_selftestProvider->recognize(request, config));
}

void RuntimeController::runSelfTestQml()
{
    if (m_selftestRunning)
        return;
    m_selftestRunning = true;
    m_selftestOk = false;
    m_selftestMessage = tr("Running self-test…");
    emit selftestFinished();

    // Reuse the same chain; mirror the outcome into the QML-visible state.
    auto promise = std::make_shared<QFutureInterface<SelfTestResult>>();
    promise->reportStarted();
    auto *watch = new QFutureWatcher<ResolvedConnection>(this);
    connect(watch, &QFutureWatcher<ResolvedConnection>::finished, this,
            [this, watch, promise]() {
                const ResolvedConnection conn = watch->result();
                watch->deleteLater();
                if (conn.baseUrl.isEmpty()) {
                    m_selftestRunning = false;
                    m_selftestOk = false;
                    m_selftestMessage =
                        conn.error.isEmpty() ? tr("Server not available") : conn.error;
                    emit selftestFinished();
                    return;
                }

                if (!m_selftestProvider)
                    m_selftestProvider = new OpenAiProvider(this);

                QFutureWatcher<OcrResult> *rw =
                    new QFutureWatcher<OcrResult>(this);
                connect(rw, &QFutureWatcher<OcrResult>::finished, this, [this, rw]() {
                    const OcrResult res =
                        rw->future().resultCount() > 0 ? rw->result()
                                                        : OcrResult::makeError(tr("No response"));
                    rw->deleteLater();
                    m_selftestRunning = false;
                    if (res.success) {
                        m_selftestOk = true;
                        m_selftestMessage = res.text;
                    } else {
                        m_selftestOk = false;
                        m_selftestMessage = res.errorMessage.isEmpty()
                                                ? tr("Recognition failed")
                                                : res.errorMessage;
                    }
                    emit selftestFinished();
                });

                OcrRequest request;
                request.image = makeTestImage();
                request.prompt = tr("Describe the text in this image in one short line.");
                request.modelId = conn.modelId;
                ProviderConfig config;
                config.baseUrl = conn.baseUrl;
                config.apiKey = conn.apiKey;
                config.timeoutMs = conn.timeoutMs;
                rw->setFuture(m_selftestProvider->recognize(request, config));
            });
    watch->setFuture(ensureConnectionReady());
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

    const ProbeResult probe = RuntimeLocator::probeCached(program, 5000);
    if (!probe.ok) {
        setStatusMessage(probe.error);
        return probe.error;
    }

    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings);
    cfg.program = program;
    QStringList args = cfg.toArguments(probe.capabilities);
    args.removeAll(QStringLiteral("--port"));

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
        connect(m_server, &LlamaServerProcess::logLineAppended, this,
                [this](const QString &) { emit serverLogChanged(); });
        connect(m_server, &LlamaServerProcess::loadProgressChanged, this,
                [this]() { setLoadProgressPercent(m_server->loadProgressPercent()); });
    } else {
        m_server->setOptions(opts);
    }

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
    m_server->stop();
    setLoadProgressPercent(-1);
    setBusyState(AppBusyState::Idle);
    setState(RuntimeState::Stopped);
    setStatusMessage(QObject::tr("Stopped"));
}

void RuntimeController::restartServer()
{
    if (m_server)
        m_server->stop();
    const QString err = startServer();
    if (!err.isEmpty())
        setStatusMessage(err);
}

QString RuntimeController::probeRuntimePath(const QString &path)
{
    const ProbeResult r = RuntimeLocator::probe(path);
    const QString summary = RuntimeLocator::probeSummary(r);
    setStatusMessage(summary);
    return summary;
}

QString RuntimeController::autoDiscoverPath()
{
    const QString found = RuntimeLocator::autoDiscover();
    if (!found.isEmpty())
        setStatusMessage(RuntimeLocator::probeSummary(RuntimeLocator::probe(found)));
    else
        setStatusMessage(QObject::tr("No llama-server binary found automatically"));
    return found;
}

QString RuntimeController::serverLog() const
{
    if (!m_server)
        return QString();
    return m_server->ringBuffer(2000).join(QStringLiteral("\n"));
}

QString RuntimeController::serverLogPath() const
{
    return m_server ? m_server->logFilePath() : QString();
}

QString RuntimeController::serverLogDir() const
{
    if (m_server) {
        const QString fp = m_server->logFilePath();
        if (!fp.isEmpty())
            return QFileInfo(fp).absolutePath();
    }
    const RuntimePaths p(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    return p.logsDir();
}

void RuntimeController::copyServerLog()
{
    QGuiApplication::clipboard()->setText(serverLog());
}

void RuntimeController::clearServerLog()
{
    if (m_server)
        m_server->clearLog();
    emit serverLogChanged();
}

void RuntimeController::openServerLogFolder()
{
    const QString dir = serverLogDir();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
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
    if (m_server && m_server->state() == RuntimeState::Starting) {
        m_server->stop();
        setLoadProgressPercent(-1);
        setState(RuntimeState::Stopped);
        setBusyState(AppBusyState::Idle);
    }
    failResolve(tr("Server start cancelled"));
    setStatusMessage(tr("Stopped"));
}

void RuntimeController::shutdownSync()
{
    if (m_server)
        m_server->shutdownSync(5000);
}

}  // namespace llocr