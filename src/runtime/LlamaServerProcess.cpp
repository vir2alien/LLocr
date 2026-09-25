#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QTcpServer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "runtime/LlamaServerProcess.h"
#include "runtime/ProcessGuard.h"

namespace llocr {

namespace {
constexpr qint64 kLogRotateSizeBytes = 5 * 1024 * 1024;   // rotate at 5 MB
constexpr int kLogRotateCheckEveryLines = 256;            // check each N lines
constexpr qint64 kRestartWindowMs = 5 * 60 * 1000;        // bounded auto-restart
constexpr int kMaxRestartsInWindow = 3;                   //   ≤3 per window
constexpr int kRestartDelayMs = 500;                      // grace before respawn
}  // namespace

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

LlamaServerProcess::LlamaServerProcess(const Options &opts, QObject *parent)
    : QObject(parent)
    , m_opts(opts)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.setProgram(m_opts.program);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif

    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            &LlamaServerProcess::onReadyRead);
    connect(&m_process, &QProcess::finished, this, &LlamaServerProcess::onProcessFinished);
}

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
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000))
            m_process.kill();
    }
}

void LlamaServerProcess::setOptions(const Options &opts)
{
    if (m_process.state() != QProcess::NotRunning)
        return;
    m_opts = opts;
    m_process.setProgram(m_opts.program);
}

QString LlamaServerProcess::start()
{
    if (m_process.state() != QProcess::NotRunning)
        return QObject::tr("Server is already running");

    m_stopRequested = false;
    spawn();
    return QString();
}

void LlamaServerProcess::spawn()
{
    m_healthReached = false;
    m_modelsProbed = false;
    m_healthInFlight = false;
    if (m_opts.port == 0)
        m_port = pickFreePort(nullptr);
    else
        m_port = m_opts.port;
    if (m_port <= 0) {
        markFailed(QObject::tr("Unable to allocate a free loopback port"));
        return;
    }
    if (m_opts.baseUrl.isEmpty()) {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(m_opts.host);
        url.setPort(m_port);
        m_healthUrl = url.toString();
    } else {
        m_healthUrl = m_opts.baseUrl;
    }

    m_attemptsTotal++;
    m_loadPercent = -1;
    setState(RuntimeState::Starting);
    setStatus(QObject::tr("Starting server (attempt %1)").arg(m_attemptsTotal));

    ProcessGuard::install(m_process);

    QStringList args = m_opts.arguments;
    if (!args.contains(QStringLiteral("--port")) && m_port > 0) {
        args.append(QStringLiteral("--port"));
        args.append(QString::number(m_port));
    }
    m_process.setArguments(args);
    if (!m_opts.workingDirectory.isEmpty())
        m_process.setWorkingDirectory(m_opts.workingDirectory);

    m_lineBuffer.clear();
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

void LlamaServerProcess::armHealthPolling()
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    if (!m_healthTimer) {
        m_healthTimer = new QTimer(this);
        m_healthTimer->setInterval(250);
        connect(m_healthTimer, &QTimer::timeout, this, [this]() {
    if (m_state != RuntimeState::Starting)
        return;
            if (m_elapsed.elapsed() > m_opts.startupTimeoutMs) {
                markFailed(QObject::tr("Server did not answer /health within %1 ms")
                               .arg(m_opts.startupTimeoutMs));
                return;
            }
            if (m_healthInFlight)
                return;
            m_healthInFlight = true;
            QNetworkRequest req(QUrl(m_healthUrl + QStringLiteral("/health")));
            req.setTransferTimeout(500);
            QNetworkReply *reply = m_net->get(req);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply]() { onHealthReply(reply); });
        });
    }
    m_healthTimer->start();
}

void LlamaServerProcess::onHealthReply(QNetworkReply *reply)
{
    reply->deleteLater();
    m_healthInFlight = false;
    if (m_state != RuntimeState::Starting)
        return;
    const int code =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code >= 200 && code < 300) {
        m_healthTimer->stop();
        m_healthReached = true;
        setState(RuntimeState::Ready);
        setStatus(QStringLiteral("Ready"));
        return;
    }
    if (!m_modelsProbed) {
        m_modelsProbed = true;
        tryModelsFallback();
    }
}

void LlamaServerProcess::tryModelsFallback()
{
    QNetworkRequest req(QUrl(m_healthUrl + QStringLiteral("/v1/models")));
    req.setTransferTimeout(3000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_state != RuntimeState::Starting)
            return;
        const int code =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError ||
            (code < 200 || code >= 300)) {
            qWarning() << "/v1/models fallback failed:" << reply->errorString();
            return;
        }
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject() ||
            !doc.object().contains(QStringLiteral("data"))) {
            qWarning() << "/v1/models fallback returned a malformed body";
            return;
        }
        m_healthTimer->stop();
        m_healthReached = true;
        setState(RuntimeState::Ready);
        setStatus(QStringLiteral("Ready"));
    });
}

void LlamaServerProcess::onReadyRead()
{
    const QByteArray data = m_process.readAllStandardOutput();
    QString text = QString::fromUtf8(data);
    if (text.isEmpty())
        return;

    m_lineBuffer += text;
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
    if (!m_logFile.isOpen()) {
        m_logFile.setFileName(m_opts.logFile);
        if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append
                            | QIODevice::Text))
            return;
        m_logStream.setDevice(&m_logFile);
    }
    if (++m_linesSinceRotateCheck >= kLogRotateCheckEveryLines) {
        m_linesSinceRotateCheck = 0;
        if (rotateLogIfNeeded()) {
            m_logFile.setFileName(m_opts.logFile);
            if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append
                                | QIODevice::Text))
                return;
            m_logStream.setDevice(&m_logFile);
        }
    }
    m_logStream << line << u'\n';
    m_logStream.flush();
}

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
}

int LlamaServerProcess::parseLoadPercent(const QString &line)
{
    if (!line.contains(QStringLiteral("tensors")))
        return -1;

    static const QRegularExpression percentRe(
        QStringLiteral("(\\d{1,5}(?:[.,]\\d{1,3})?)\\s*%"));

    QRegularExpressionMatchIterator it = percentRe.globalMatch(line);
    if (!it.hasNext())
        return -1;
    QRegularExpressionMatch last;
    while (it.hasNext())
        last = it.next();

    bool ok = false;
    const QString token = QString(last.captured(1)).replace(u',', u'.');
    const double value = token.toDouble(&ok);
    if (!ok)
        return -1;
    return qBound(0, int(qRound(value)), 100);
}

bool LlamaServerProcess::rotateLogIfNeeded()
{
    const QFileInfo fi(m_opts.logFile);
    if (!fi.exists() || fi.size() < kLogRotateSizeBytes)
        return false;
    closeLogFile();
    const QString base = m_opts.logFile;
    QFile::remove(base + QStringLiteral(".3"));
    QFile::rename(base + QStringLiteral(".2"), base + QStringLiteral(".3"));
    QFile::rename(base + QStringLiteral(".1"), base + QStringLiteral(".2"));
    QFile::rename(base, base + QStringLiteral(".1"));
    return true;
}

void LlamaServerProcess::onProcessFinished(int /*exitCode*/, QProcess::ExitStatus)
{
    if (m_healthTimer)
        m_healthTimer->stop();

    clearOwnerJson();

    if (m_stopRequested) {
        setState(RuntimeState::Stopped);
        setStatus(QObject::tr("Stopped"));
        return;
    }

    if (m_lastError.isEmpty())
        m_lastError = QObject::tr("Server process exited unexpectedly");
    setStatus(m_lastError);

    const bool restartEligible = m_opts.autoRestart && !m_stopRequested
                                 && m_state != RuntimeState::Failed;
    int remaining = 0;
    if (restartEligible) {
        if (!m_restartWindow.isValid() || m_restartWindow.elapsed() > kRestartWindowMs) {
            m_restartWindowCount = 0;
            m_restartWindow.restart();
        }
        remaining = kMaxRestartsInWindow - m_restartWindowCount;
    }
    if ((!restartEligible || remaining <= 0) && m_state != RuntimeState::Failed)
        markFailed(m_lastError);

    if (restartEligible && remaining > 0) {
        m_restartWindowCount++;
        m_autoRestartScheduled = true;
        setStatus(QObject::tr("Server crashed — restarting…"));
        setState(RuntimeState::Starting);
        QTimer::singleShot(kRestartDelayMs, this, [this]() {
            m_autoRestartScheduled = false;
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

void LlamaServerProcess::stop(unsigned graceMs)
{
    m_stopRequested = true;
    if (m_healthTimer)
        m_healthTimer->stop();

    if (m_process.state() == QProcess::NotRunning) {
        setState(RuntimeState::Stopped);
        setStatus(QObject::tr("Stopped"));
        clearOwnerJson();
        return;
    }
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
    m_stopRequested = true;
    if (m_healthTimer)
        m_healthTimer->stop();
    if (m_killTimer)
        m_killTimer->stop();
    closeLogFile();
    if (!m_opts.stopOnExit) {
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

void LlamaServerProcess::retranslate()
{
    switch (m_state) {
    case RuntimeState::Starting:
        if (m_loadPercent >= 0)
            setStatus(QObject::tr("Loading model… %1%").arg(m_loadPercent));
        else
            setStatus(QObject::tr("Starting server (attempt %1)").arg(m_attemptsTotal));
        break;
    case RuntimeState::Stopping:
        setStatus(QObject::tr("Stopping…"));
        break;
    case RuntimeState::Stopped:
        setStatus(QObject::tr("Stopped"));
        break;
    case RuntimeState::NotConfigured:
    case RuntimeState::Ready:
    case RuntimeState::Failed:
        break;
    }
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
    return m_port;
}

void LlamaServerProcess::writeOwnerJson()
{
    if (m_opts.ownerJsonPath.isEmpty())
        return;
    QJsonObject o;
    o.insert(QStringLiteral("pid"), m_process.processId());
    o.insert(QStringLiteral("parentPid"), double(ProcessGuard::currentPid()));
    o.insert(QStringLiteral("port"), m_port);
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
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    appendLine(reason);  // ring + log + emit
}

}  // namespace llocr