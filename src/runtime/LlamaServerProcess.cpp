#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTextStream>
#include <QTcpServer>
#include <QUrl>

#include "runtime/LlamaServerProcess.h"
#include "runtime/ProcessGuard.h"

namespace llocr {

// ---------------------------------------------------------------------------
// Port selection (§5 task 4 / ADR 33)
// ---------------------------------------------------------------------------

int LlamaServerProcess::pickFreePort(QString *error)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0)) {
            const int port = probe.serverPort();
            probe.close();
            if (port > 0 && port <= 65535)
                return port;
        }
    }
    if (error)
        *error = QObject::tr("Unable to allocate a free loopback port");
    return 0;
}

// ---------------------------------------------------------------------------
// Construction / start
// ---------------------------------------------------------------------------

LlamaServerProcess::LlamaServerProcess(const Options &opts, QObject *parent)
    : QObject(parent)
    , m_opts(opts)
{
    // Only setProgram/setArguments — never a shell (7.1).
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.setProgram(m_opts.program);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif

    // Output + exit capture: connected once, so restarts never duplicate.
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            &LlamaServerProcess::onReadyRead);
    connect(&m_process, &QProcess::finished, this, &LlamaServerProcess::onProcessFinished);
}

// §3.3: flush and close the persistent log handle (stop/shutdown/destructor).
void LlamaServerProcess::closeLogFile()
{
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

LlamaServerProcess::~LlamaServerProcess()
{
    closeLogFile();
}

void LlamaServerProcess::setOptions(const Options &opts)
{
    m_opts = opts;
    m_process.setProgram(m_opts.program);
}

QString LlamaServerProcess::start()
{
    if (m_process.state() != QProcess::NotRunning)
        return QObject::tr("Server is already running");

    if (m_opts.port == 0) {
        QString portErr;
        m_opts.port = pickFreePort(&portErr);
        if (m_opts.port == 0)
            return portErr;
    }

    m_healthReached = false;
    m_stopRequested = false;
    m_modelsProbed = false;
    // §3.5: assemble via QUrl so an IPv6 host (::1) is bracketed correctly.
    if (m_opts.baseUrl.isEmpty()) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(m_opts.host);
        url.setPort(m_opts.port);
        m_healthUrl = url.toString();
    } else {
        m_healthUrl = m_opts.baseUrl;
    }
    spawn();
    return QString();
}

void LlamaServerProcess::spawn()
{
    m_attemptsTotal++;
    m_loadPercent = -1;
    setState(RuntimeState::Starting);
    setStatus(QObject::tr("Starting server (attempt %1)").arg(m_attemptsTotal));

    ProcessGuard::install(m_process);

    QStringList args = m_opts.arguments;
    if (m_opts.port > 0) {
        args.append(QStringLiteral("--port"));
        args.append(QString::number(m_opts.port));
    }
    m_process.setArguments(args);
    if (!m_opts.workingDirectory.isEmpty())
        m_process.setWorkingDirectory(m_opts.workingDirectory);

    // Capture output for the ring buffer / rotating log.
    m_lineBuffer.clear();
    // §H.7 task 1: a fresh llama.cpp spawn is fast (fork/exec), so a 5 s upper
    // bound on the synchronous wait keeps worst-case main-thread blocking low
    // without failing legitimately slow first starts (cold disk, AV scanning
    // on Windows). The real startup cost (model load) is covered by the health
    // watchdog below.
    m_process.start(QIODevice::ReadOnly);
    if (!m_process.waitForStarted(5000)) {
        m_lastError = m_process.errorString();
        onProcessFinished(0, QProcess::CrashExit);
        return;
    }
    ProcessGuard::attachParent(m_process);

    if (!m_opts.logFile.isEmpty())
        QDir().mkpath(QFileInfo(m_opts.logFile).absolutePath());
    writeOwnerJson();

    m_elapsed.restart();
    armHealthPolling();
}

// ---------------------------------------------------------------------------
// Health watchdog (§5 task 3)
// ---------------------------------------------------------------------------

void LlamaServerProcess::armHealthPolling()
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    if (!m_healthTimer) {
        m_healthTimer = new QTimer(this);
        // §H.7 task 1: health-interval tuned to fast starts — loopback probes
        // are cheap, and halves the time-to-Ready detection for a quick model
        // load. The startup timeout, not the interval, bounds the failure case.
        m_healthTimer->setInterval(250);
        connect(m_healthTimer, &QTimer::timeout, this, [this]() {
    if (m_state != RuntimeState::Starting)
        return;
            if (m_elapsed.elapsed() > m_opts.startupTimeoutMs) {
                markFailed(QObject::tr("Server did not answer /health within %1 ms")
                               .arg(m_opts.startupTimeoutMs));
                return;
            }
            QNetworkReply *reply =
                m_net->get(QNetworkRequest(QUrl(m_healthUrl + QStringLiteral("/health"))));
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply]() { onHealthReply(reply); });
        });
    }
    m_healthTimer->start();
}

void LlamaServerProcess::onHealthReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_state != RuntimeState::Starting)
        return;
    const int code =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code >= 200 && code < 300) {
        m_healthTimer->stop();
        m_healthReached = true;
        setState(RuntimeState::Ready);
        setStatus(QStringLiteral("Ready"));
        emit healthReached();
        return;
    }
    // §2.9: /health unavailable — try /v1/models once per startup attempt
    // before declaring failure (some builds/servers do not expose /health).
    if (!m_modelsProbed) {
        m_modelsProbed = true;
        tryModelsFallback();
    }
    // otherwise keep polling until the timeout fires
}

// §2.9 fallback probe: HTTP 200 with a JSON body containing "data" counts as
// healthy. Runs at most once per startup attempt (m_modelsProbed guard).
void LlamaServerProcess::tryModelsFallback()
{
    QNetworkReply *reply =
        m_net->get(QNetworkRequest(QUrl(m_healthUrl + QStringLiteral("/v1/models"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_state != RuntimeState::Starting)
            return;
        const int code =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (code >= 200 && code < 300 && doc.object().contains(QStringLiteral("data"))) {
            m_healthTimer->stop();
            m_healthReached = true;
            setState(RuntimeState::Ready);
            setStatus(QStringLiteral("Ready"));
            emit healthReached();
        }
        // Not healthy via the fallback → the regular polling continues.
    });
}

// ---------------------------------------------------------------------------
// Output capture → ring + rolling file
// ---------------------------------------------------------------------------

void LlamaServerProcess::onReadyRead()
{
    const QByteArray data = m_process.readAllStandardOutput();
    QString text = QString::fromUtf8(data);
    if (text.isEmpty())
        return;

    m_lineBuffer += text;
    // Split complete lines; keep the trailing partial for the next read.
    int nl;
    while ((nl = m_lineBuffer.indexOf(u'\n')) >= 0) {
        QString line = m_lineBuffer.left(nl);
        if (line.endsWith(u'\r'))
            line.chop(1);
        appendLine(line);
        m_lineBuffer.remove(0, nl + 1);
    }
}

void LlamaServerProcess::appendLine(const QString &line)
{
    if (line.trimmed().isEmpty())
        return;
    m_ring.append(line);
    if (m_ring.size() > m_ringMaxLines)
        m_ring.remove(0, m_ring.size() - m_ringMaxLines);
    appendLogFile(line);
    emit logLineAppended(line);
    classifyLine(line);
}

void LlamaServerProcess::appendLogFile(const QString &line)
{
    if (m_opts.logFile.isEmpty())
        return;
    // §3.3: keep one handle open in append mode; check rotation only every
    // 256 lines (the 5 MB threshold is far above a per-line write).
    if (!m_logFile.isOpen()) {
        m_logFile.setFileName(m_opts.logFile);
        if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append
                            | QIODevice::Text))
            return;
        m_logStream.setDevice(&m_logFile);
    }
    if (++m_linesSinceRotateCheck >= 256) {
        m_linesSinceRotateCheck = 0;
        rotateLogIfNeeded();
        if (!m_logFile.isOpen())  // rotated away underneath us — reopen lazily
            return;
    }
    m_logStream << line << u'\n';
    m_logStream.flush();
}

// §H.7 task 2: classify a raw llama.cpp stderr line into a stable, readable
// status. The important case is model-load progress, surfaced as an explicit
// percentage (llama.cpp prints "loading tensors, NN%" and "load_tensors:
// NN%"); significant milestones get a short placeholder; everything else is
// skipped so the status never becomes a noisy dump of arbitrary "load"/"model"
// lines.
void LlamaServerProcess::classifyLine(const QString &line)
{
    const int pct = parseLoadPercent(line);
    if (pct >= 0) {
        m_loadPercent = pct;
        setStatus(QObject::tr("Loading model… %1%").arg(pct));
        emit loadProgressChanged();
        return;
    }
    if (line.contains(QStringLiteral("llama_new_context_with_model"))) {
        setStatus(QObject::tr("Preparing context…"));
        return;
    }
    // §3.2: non-milestone lines are intentionally not surfaced as status —
    // emitting raw print_info/tensor lines caused a status-signal storm while
    // the model loads.
}

int LlamaServerProcess::parseLoadPercent(const QString &line)
{
    // Progress is only recognized on tensor-loading lines; the "%"-less
    // `load_tensors:` buffer-size lines (also containing "tensors") are
    // naturally excluded because they carry no percent sign.
    if (!line.contains(QStringLiteral("tensors")))
        return -1;
    const int pctPos = line.lastIndexOf(u'%');
    if (pctPos < 0)
        return -1;
    int start = pctPos;
    while (start > 0 && (line.at(start - 1).isDigit() || line.at(start - 1) == u'.'
                         || line.at(start - 1) == u','))
        --start;
    bool ok = false;
    const double value = line.mid(start, pctPos - start).toDouble(&ok);
    if (!ok)
        return -1;
    return qBound(0, int(qRound(value)), 100);
}

void LlamaServerProcess::rotateLogIfNeeded()
{
    const QFileInfo fi(m_opts.logFile);
    if (!fi.exists() || fi.size() < 5 * 1024 * 1024)
        return;
    // Close the persistent handle so the rename works, then reopen lazily.
    closeLogFile();
    // Rotate *.log -> *.1 -> *.2 -> *.3 (drop the oldest).
    const QString base = m_opts.logFile;
    QFile::remove(base + QStringLiteral(".3"));
    QFile::rename(base + QStringLiteral(".2"), base + QStringLiteral(".3"));
    QFile::rename(base + QStringLiteral(".1"), base + QStringLiteral(".2"));
    QFile::rename(base, base + QStringLiteral(".1"));
}

// ---------------------------------------------------------------------------
// Process exit / auto-restart (§5 task 3)
// ---------------------------------------------------------------------------

void LlamaServerProcess::onProcessFinished(int /*exitCode*/, QProcess::ExitStatus)
{
    if (m_healthTimer)
        m_healthTimer->stop();

    if (m_stopRequested) {
        setState(RuntimeState::Stopped);
        setStatus(QObject::tr("Stopped"));
        if (m_opts.stopOnExit)
            clearOwnerJson();
        return;
    }

    // If the port was busy, surface a specific, actionable error (7.5).
    if (m_lastError.isEmpty())
        m_lastError = QObject::tr("Server process exited unexpectedly");
    setStatus(m_lastError);

    // §3.1: when auto-restart is still eligible, do not pass through Failed —
    // enter it only once the restart budget is exhausted or restart is off.
    const bool restartEligible = m_opts.autoRestart && !m_stopRequested;
    int remaining = 0;
    if (restartEligible) {
        if (!m_restartWindow.isValid() || m_restartWindow.elapsed() > 5 * 60 * 1000) {
            m_restartWindowCount = 0;
            m_restartWindow.restart();
        }
        remaining = 3 - m_restartWindowCount;
    }
    if (!restartEligible || remaining <= 0)
        markFailed(m_lastError);

    if (restartEligible && remaining > 0) {
        m_restartWindowCount++;
        m_autoRestartScheduled = true;
        QTimer::singleShot(500, this, [this]() {
            m_autoRestartScheduled = false;
            // stop()/shutdownSync() may have requested a stop during the
            // 500 ms restart window — do not resurrect the server.
            if (m_stopRequested)
                return;
            spawn();
        });
        return;
    }
    if (restartEligible) {
        setStatus(QObject::tr("Auto-restart limit reached; giving up"));
        emit statusMessageChanged();
    }
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

void LlamaServerProcess::stop(unsigned graceMs)
{
    m_stopRequested = true;
    if (m_healthTimer)
        m_healthTimer->stop();

    if (m_process.state() == QProcess::NotRunning) {
        setState(RuntimeState::Stopped);
        setStatus(QObject::tr("Stopped"));
        if (m_opts.stopOnExit)
            clearOwnerJson();
        return;
    }

    // §2.7: asynchronous stop — terminate now, kill after the grace period if
    // the child is still alive. The final Stopped state is entered from
    // onProcessFinished() when the child actually exits, so the GUI thread
    // never blocks for up to ~7 s.
    setState(RuntimeState::Stopping);
    setStatus(QObject::tr("Stopping…"));
    m_process.terminate();
    if (!m_killTimer) {
        m_killTimer = new QTimer(this);
        m_killTimer->setSingleShot(true);
        connect(m_killTimer, &QTimer::timeout, this, [this]() {
            if (m_process.state() != QProcess::NotRunning)
                m_process.kill();
        });
    }
    m_killTimer->start(int(graceMs));
}

void LlamaServerProcess::shutdownSync(unsigned baseTimeoutMs)
{
    // Mark as user-requested so onProcessFinished() cannot take the
    // failure/auto-restart branch during shutdown.
    m_stopRequested = true;
    if (m_healthTimer)
        m_healthTimer->stop();
    if (m_killTimer)
        m_killTimer->stop();
    closeLogFile();
    if (!m_opts.stopOnExit) {
        // Intentionally leave the server running; owner file stays so the next
        // launch offers reuse or kill.
        return;
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        m_process.waitForFinished(int(baseTimeoutMs));
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
    clearOwnerJson();
    setState(RuntimeState::Stopped);
    setStatus(QObject::tr("Stopped"));
}

bool LlamaServerProcess::isRunning() const
{
    return m_process.state() != QProcess::NotRunning
           && (m_state == RuntimeState::Starting || m_state == RuntimeState::Ready
               || m_state == RuntimeState::Stopping);
}

RuntimeState LlamaServerProcess::state() const
{
    return m_state;
}

QString LlamaServerProcess::statusMessage() const
{
    return m_status;
}

QString LlamaServerProcess::lastError() const
{
    return m_lastError;
}

QStringList LlamaServerProcess::ringBuffer(int maxLines) const
{
    if (maxLines < 0 || maxLines >= m_ring.size())
        return m_ring;
    return m_ring.mid(m_ring.size() - maxLines);
}

QString LlamaServerProcess::logFilePath() const
{
    return m_opts.logFile;
}

void LlamaServerProcess::clearLog()
{
    m_ring.clear();
    emit logLineAppended(QString());
}

int LlamaServerProcess::startCount() const
{
    return m_attemptsTotal;
}

int LlamaServerProcess::restartCount() const
{
    return m_restartWindowCount;
}

int LlamaServerProcess::resolvedPort() const
{
    return m_opts.port;
}

// ---------------------------------------------------------------------------
// owner.json (§5.4 macOS best-effort; written on all platforms, used on mac)
// ---------------------------------------------------------------------------

void LlamaServerProcess::writeOwnerJson()
{
    if (m_opts.ownerJsonPath.isEmpty())
        return;
    QJsonObject o;
    o.insert(QStringLiteral("pid"), m_process.processId());
    o.insert(QStringLiteral("parentPid"), double(ProcessGuard::currentPid()));
    o.insert(QStringLiteral("port"), m_opts.port);
    o.insert(QStringLiteral("program"), m_process.program());
    // Atomic write (§7.2).
    QSaveFile f(m_opts.ownerJsonPath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QTextStream out(&f);
    out << QJsonDocument(o).toJson(QJsonDocument::Compact);
    out.flush();
    f.commit();
}

void LlamaServerProcess::clearOwnerJson()
{
    if (!m_opts.ownerJsonPath.isEmpty())
        QFile::remove(m_opts.ownerJsonPath);
}

void LlamaServerProcess::setState(RuntimeState next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void LlamaServerProcess::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusMessageChanged();
}

void LlamaServerProcess::markFailed(const QString &reason)
{
    m_lastError = reason;
    if (m_healthTimer)
        m_healthTimer->stop();
    setState(RuntimeState::Failed);
    setStatus(reason);
    appendLine(reason);  // ring + log + emit
}

}  // namespace llocr