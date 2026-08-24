#include <QFuture>
#include <QFutureInterface>
#include <QDir>
#include <QFileInfo>

#include "app/SettingsStore.h"
#include "runtime/LlamaServerProcess.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/RuntimePaths.h"
#include "runtime/ServerLaunchConfig.h"

namespace llocr {

RuntimeController::RuntimeController(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // configValid (§1.4): has a server binary been chosen and does the model
    // file exist? In Stage A neither a binary nor a model is mandatory yet, so
    // it reflects whether a server path is configured. Stage B/C tighten this.
    m_configValid = !m_settings.serverPath().trimmed().isEmpty();

    // Keep the QML-visible configValid in sync as the user edits Settings.
    connect(&m_settings, &SettingsStore::serverPathChanged, this, [this]() {
        const bool valid = !m_settings.serverPath().trimmed().isEmpty();
        if (valid == m_configValid)
            return;
        m_configValid = valid;
        emit configValidChanged();
    });
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

ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
{
    return settings.connectionMode() == QString::fromUtf8(SettingsStore::kModeManaged)
               ? ConnectionMode::Managed
               : ConnectionMode::External;
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

QFuture<ResolvedConnection> RuntimeController::ensureConnectionReady()
{
    // Stage A: only the External path (ADR 26). Managed is wired in Stage G-core.
    // Cancelling a pending start is a no-op here since External resolves
    // synchronously and never transitions through StartingRuntime.
    QFutureInterface<ResolvedConnection> promise;
    promise.reportStarted();
    const ResolvedConnection conn = resolveExternal();
    promise.reportResult(conn);
    promise.reportFinished();
    return promise.future();
}

QFuture<ResolvedConnection> RuntimeController::runSelfTest()
{
    // Placeholder until Stage G-core. External has nothing to self-test here.
    return ensureConnectionReady();
}

void RuntimeController::cancelPendingStart()
{
    // Managed-only. In External there is never a pending start to cancel.
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

    // Sanity: refuse to hand a below-minimum binary to the user early.
    // Bounded probe so a hung binary cannot stall the Settings dialog for
    // long (worst case: --version + --help each up to the timeout).
    const ProbeResult probe = RuntimeLocator::probe(program, 5000);
    if (!probe.ok) {
        setStatusMessage(probe.error);
        return probe.error;
    }

    RuntimePaths paths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
    paths.ensureDirectories();

    ServerLaunchConfig cfg = ServerLaunchConfig::fromSettings(m_settings);
    cfg.program = program;
    // toArguments() would add --port for cfg.port; the process owns the port
    // (auto-pick or fixed), so strip it here — it re-adds the resolved one.
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
        connect(m_server, &LlamaServerProcess::stateChanged, this, [this]() {
            setState(m_server->state());
        });
        connect(m_server, &LlamaServerProcess::statusMessageChanged, this, [this]() {
            setStatusMessage(m_server->statusMessage());
        });
        connect(m_server, &LlamaServerProcess::logLineAppended, this, [this]() {
            emit serverLogChanged();
        });
    } else {
        // Re-apply options (settings may have changed since the last run).
        m_server->setOptions(opts);
    }

    setBusyState(AppBusyState::StartingRuntime);
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

void RuntimeController::shutdownSync()
{
    if (m_server)
        m_server->shutdownSync(5000);
}

}  // namespace llocr