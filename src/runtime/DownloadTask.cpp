#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTextStream>
#include <QUrl>

#include <utility>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "runtime/DownloadTask.h"

namespace llocr {

namespace {

// Force buffered pages to the backing store before an atomic rename (§ Stage C
// task 1: "flush + fsync" — QFile::flush() only drains the C++ side).
void flushToDisk(QFile &file)
{
#ifdef Q_OS_WIN
    // _get_osfhandle converts the CRT fd to a native HANDLE; the CRT fd itself
    // must not be reinterpreted as one.
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    if (h != INVALID_HANDLE_VALUE && !FlushFileBuffers(h))
        qWarning("FlushFileBuffers failed for download file (error %lu)",
                 static_cast<unsigned long>(GetLastError()));
#else
    const int fd = static_cast<int>(file.handle());
    if (fd >= 0)
        ::fsync(fd);
#endif
}

// "bytes 0-1023/4096" | "bytes 1024-4095/*" (total unknown). Returns false on
// any mismatch: wrong unit, missing dash/slash, non-numeric bounds, end<start.
bool parseContentRange(const QString &value, qint64 &start, qint64 &end, qint64 &total)
{
    const QString v = value.trimmed();
    if (!v.startsWith(QStringLiteral("bytes "), Qt::CaseInsensitive))
        return false;
    const QString rest = v.mid(6).trimmed();
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return false;
    const QString rangePart = rest.left(slash);
    const QString totalPart = rest.mid(slash + 1);
    const int dash = rangePart.indexOf(QLatin1Char('-'));
    if (dash < 0)
        return false;

    bool ok1 = false;
    bool ok2 = false;
    start = rangePart.left(dash).toLongLong(&ok1);
    end = rangePart.mid(dash + 1).toLongLong(&ok2);
    if (!ok1 || !ok2 || end < start)
        return false;

    if (totalPart == QLatin1Char('*')) {
        total = -1;
    } else {
        total = totalPart.toLongLong(&ok1);
        if (!ok1 || total < 0)
            return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// File-name sanitization
// ---------------------------------------------------------------------------

// Stage C: “collisions resolved by a suffix”. If <dir>/<name> (the final
// target) already exists, append -1, -2, … before the extension. An existing
// `.part` is deliberately NOT treated as a collision: resume must reuse the
// exact same part path, so a seeded/partial `.part` must not push the task to a
// different `-N` name and strand the resumable data.
QString resolveFileNameCollision(const QString &dir, const QString &name)
{
    if (!QFileInfo::exists(QDir(dir).filePath(name)))
        return name;

    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString base = (dot > 0) ? name.left(dot) : name;
    const QString ext = (dot > 0) ? name.mid(dot) : QString();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = base + QLatin1Char('-') + QString::number(i) + ext;
        if (!QFileInfo::exists(QDir(dir).filePath(candidate)))
            return candidate;
    }
    return name;  // give up and let the write fail naturally
}

QString sanitizeFileName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (u < 0x20)
            continue;                       // control characters
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':'))
            continue;                       // path separators / drive colon
        out.append(c);
    }
    // Trim bracketing whitespace and dots: blocks hidden files and a bare "..".
    out = out.trimmed();
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);

    if (out.isEmpty())
        out = QStringLiteral("download");

    // Windows reserved device names (case-insensitive, any extension).
    static const QStringList reserved = {QStringLiteral("CON"), QStringLiteral("PRN"),
                                         QStringLiteral("AUX"), QStringLiteral("NUL"),
                                         QStringLiteral("COM1"), QStringLiteral("COM2"),
                                         QStringLiteral("COM3"), QStringLiteral("COM4"),
                                         QStringLiteral("COM5"), QStringLiteral("COM6"),
                                         QStringLiteral("COM7"), QStringLiteral("COM8"),
                                         QStringLiteral("COM9"), QStringLiteral("LPT1"),
                                         QStringLiteral("LPT2"), QStringLiteral("LPT3"),
                                         QStringLiteral("LPT4"), QStringLiteral("LPT5"),
                                         QStringLiteral("LPT6"), QStringLiteral("LPT7"),
                                         QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    if (reserved.contains(out.section(QLatin1Char('.'), 0, 0).toUpper()))
        out.prepend(QLatin1Char('_'));

    return out;
}

// ---------------------------------------------------------------------------
// Construction / control
// ---------------------------------------------------------------------------

DownloadTask::DownloadTask(const Request &request, QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_request(request)
    , m_nam(nam)
    , m_targetDir(request.targetDir)
    , m_fileName(resolveFileNameCollision(request.targetDir, sanitizeFileName(request.fileName)))
    , m_finalPath(QDir(m_targetDir).filePath(m_fileName))
    , m_partPath(partPathFor(m_finalPath))
    , m_metaPath(metaPathFor(m_partPath))
    , m_freeBytesQuery([](const QString &dirPath) {
          return QStorageInfo(dirPath).bytesAvailable();
      })
{
}

QString DownloadTask::expectedSha256() const
{
    return m_request.sha256;
}

void DownloadTask::setFreeBytesQuery(FreeBytesQuery query)
{
    if (query)
        m_freeBytesQuery = std::move(query);
}

void DownloadTask::setState(State next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void DownloadTask::fail(const QString &message)
{
    closeFile();
    m_error = message;
    setState(State::Failed);
}

// ---------------------------------------------------------------------------
// start() — inspect .part/.meta, decide resume, issue the first request
// ---------------------------------------------------------------------------

void DownloadTask::start()
{
    if (m_state == State::Running || m_state == State::Verifying)
        return;

    m_cancelRequested = false;
    m_pauseRequested = false;
    m_abortRequested = false;
    m_error.clear();

    if (!QDir().mkpath(m_targetDir)) {
        fail(QObject::tr("Unable to create target directory: %1").arg(m_targetDir));
        emit downloadFinished(false);
        return;
    }

    // Inspect an existing partial download.
    m_resumeBytes = 0;
    m_resumeEtag.clear();
    m_resumeLastModified.clear();
    m_resumeExpectedTotal = -1;
    const QFileInfo partInfo(m_partPath);
    if (partInfo.exists() && partInfo.size() > 0) {
        m_resumeBytes = partInfo.size();
        readMeta();
    }

    // A resume is only safe with a validator; otherwise fall back to a fresh
    // download (the stale .part is truncated on the 200 path).
    m_resumeRequested = m_resumeBytes > 0 && hasResumeValidator();
    m_ifRangeValue = m_resumeRequested ? ifRangeValue() : QString();

    // Pre-start free-space check (only when the remaining size is known).
    if (m_resumeExpectedTotal > 0 && m_resumeExpectedTotal > m_resumeBytes) {
        const qint64 remaining = m_resumeExpectedTotal - m_resumeBytes;
        if (m_freeBytesQuery(m_targetDir) < remaining) {
            fail(QObject::tr("Not enough free space on the target volume"));
            emit downloadFinished(false);
            return;
        }
    }

    // Position the hash over the bytes already on disk; stream the rest.
    m_hash.reset();
    if (m_resumeRequested) {
        m_receivedBytes = m_resumeBytes;
        hashExistingPart();
    } else {
        m_receivedBytes = 0;
    }
    m_totalBytes = m_resumeRequested ? m_resumeExpectedTotal : -1;
    m_speedBps = 0;
    m_etaSec = 0;
    m_speedClockValid = false;

    m_effectiveUrl = m_request.url;
    m_authorization = m_request.authorization;
    m_redirectCount = 0;

    if (!isAllowedUrl(m_effectiveUrl)) {
        fail(QObject::tr("Only https download URLs are allowed"));
        emit downloadFinished(false);
        return;
    }

    setState(State::Running);
    issueRequest();
}

void DownloadTask::issueRequest()
{
    if (!m_nam) {
        fail(QObject::tr("No network manager"));
        emit downloadFinished(false);
        return;
    }

    QNetworkRequest req(m_effectiveUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    if (!m_authorization.isEmpty())
        req.setRawHeader("Authorization", m_authorization.toUtf8());
    if (m_resumeRequested && m_receivedBytes > 0 && !m_ifRangeValue.isEmpty()) {
        req.setRawHeader("Range", QStringLiteral("bytes=%1-").arg(m_receivedBytes).toUtf8());
        req.setRawHeader("If-Range", m_ifRangeValue.toUtf8());
    }

    m_fileOpen = false;
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::metaDataChanged, this,
            [this, reply = m_reply]() { onMetadata(reply); });
    connect(m_reply, &QNetworkReply::readyRead, this,
            [this, reply = m_reply]() { onData(reply); });
    connect(m_reply, &QNetworkReply::finished, this,
            [this, reply = m_reply]() { onFinished(reply); });
}

bool DownloadTask::hasResumeValidator() const
{
    return !m_resumeEtag.isEmpty() || !m_resumeLastModified.isEmpty();
}

QString DownloadTask::ifRangeValue() const
{
    // Weak ETags (W/…) must not be used for If-Range (RFC 7233); fall back to
    // the Last-Modified date when present.
    const bool weak = m_resumeEtag.startsWith(QStringLiteral("W/"));
    if (!m_resumeEtag.isEmpty() && !weak)
        return m_resumeEtag;
    if (!m_resumeLastModified.isEmpty())
        return m_resumeLastModified;
    return QString();
}

void DownloadTask::hashExistingPart()
{
    QFile existing(m_partPath);
    if (!existing.open(QIODevice::ReadOnly))
        return;  // best-effort; the caller already failed safe on write errors
    const qint64 chunkSize = 64 * 1024;
    while (!existing.atEnd()) {
        const QByteArray chunk = existing.read(chunkSize);
        if (!chunk.isEmpty())
            m_hash.addData(chunk);
    }
    existing.close();
}

// ---------------------------------------------------------------------------
// Metadata (headers) — classify 206 vs 200 and validate Content-Range
// ---------------------------------------------------------------------------

void DownloadTask::onMetadata(QNetworkReply *reply)
{
    if (m_reply != reply || m_state != State::Running)
        return;

    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirect.isValid())
        return;  // handled in onFinished via handleRedirect()

    // Capture validators for the resume metadata before anything else.
    const QString etag = QString::fromLatin1(reply->rawHeader("ETag"));
    const QString lastModified = QString::fromLatin1(reply->rawHeader("Last-Modified"));

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 206) {
        qint64 start = -1;
        qint64 end = -1;
        qint64 total = -1;
        const QByteArray rangeReply = reply->rawHeader("Content-Range");
        const bool okRange = parseContentRange(QString::fromLatin1(rangeReply), start, end, total);
        const bool totalMatches = !m_resumeRequested || m_resumeExpectedTotal <= 0
                                  || total <= 0 || total == m_resumeExpectedTotal;
        if (!okRange || start != m_receivedBytes || !totalMatches) {
            restartFresh(reply);
            return;
        }
        if (total > 0)
            m_totalBytes = total;
        m_resumeEtag = etag;
        m_resumeLastModified = lastModified;
        openForAppend();
    } else if (status == 200) {
        // Whole representation: either a fresh download or the server ignored
        // Range / the validator no longer matched — restart from scratch.
        const qint64 contentLength =
            reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        m_totalBytes = contentLength > 0 ? contentLength : -1;
        m_receivedBytes = 0;
        m_resumeRequested = false;
        m_resumeEtag = etag;
        m_resumeLastModified = lastModified;
        m_hash.reset();
        openForFresh();
    } else {
        fail(QObject::tr("HTTP error %1").arg(status));
        reply->abort();
        return;
    }

    // Post-header free-space check once the remaining size is known.
    if (m_totalBytes > 0 && m_receivedBytes < m_totalBytes) {
        if (m_freeBytesQuery(m_targetDir) < (m_totalBytes - m_receivedBytes)) {
            fail(QObject::tr("Not enough free space on the target volume"));
            reply->abort();
            return;
        }
    }

    persistMeta();
}

void DownloadTask::openForFresh()
{
    closeFile();
    m_file.setFileName(m_partPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QObject::tr("Unable to open %1 for writing").arg(m_partPath));
        return;
    }
    m_fileOpen = true;
}

void DownloadTask::openForAppend()
{
    closeFile();
    m_file.setFileName(m_partPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(QObject::tr("Unable to open %1 for writing").arg(m_partPath));
        return;
    }
    m_fileOpen = true;
}

void DownloadTask::restartFresh(QNetworkReply *reply)
{
    closeFile();
    // Drop the unusable partial state, then re-request the whole object.
    m_resumeRequested = false;
    m_ifRangeValue.clear();
    m_receivedBytes = 0;
    m_totalBytes = -1;
    m_hash.reset();
    QFile::remove(m_partPath);
    QFile::remove(m_metaPath);

    // Detach from the reply BEFORE aborting it: abort() emits finished()
    // synchronously, and that handler must treat this reply as no longer
    // current rather than turning the restart into a failure.
    m_reply = nullptr;
    reply->abort();
    reply->deleteLater();
    issueRequest();
}

// ---------------------------------------------------------------------------
// Body streaming
// ---------------------------------------------------------------------------

void DownloadTask::onData(QNetworkReply *reply)
{
    if (m_reply != reply || m_state != State::Running || !m_fileOpen)
        return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;

    if (m_file.write(chunk) != chunk.size()) {
        fail(QObject::tr("Write error while downloading %1").arg(m_fileName));
        reply->abort();
        return;
    }
    m_hash.addData(chunk);
    m_receivedBytes += chunk.size();
    updateProgress();
}

void DownloadTask::updateProgress()
{
    const bool firstSample = !m_speedClockValid;
    if (firstSample) {
        m_speedClock.start();
        m_speedSampleBytes = m_receivedBytes;
        m_speedClockValid = true;
        emit progressChanged();
        return;
    }
    const qint64 elapsedMs = qMax<qint64>(1, m_speedClock.elapsed());
    // Throttle UI notifications to the same 200 ms cadence as speed/ETA;
    // m_receivedBytes stays exact.
    if (elapsedMs >= 200) {
        emit progressChanged();
        const qint64 delta = m_receivedBytes - m_speedSampleBytes;
        m_speedBps = static_cast<int>(delta * 1000 / elapsedMs);
        m_speedSampleBytes = m_receivedBytes;
        m_speedClock.restart();
        m_etaSec = (m_speedBps > 0 && m_totalBytes > 0 && m_receivedBytes < m_totalBytes)
                       ? static_cast<int>((m_totalBytes - m_receivedBytes) / m_speedBps)
                       : 0;
    }
}

// ---------------------------------------------------------------------------
// Finish — redirects, cancel/abort, or final verification
// ---------------------------------------------------------------------------

void DownloadTask::onFinished(QNetworkReply *reply)
{
    if (m_reply != reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();

    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirect.isValid()) {
        handleRedirect(redirect);
        return;
    }

    if (m_cancelRequested) {
        closeFile();
        if (m_cancelDeletePartial) {
            QFile::remove(m_partPath);
            QFile::remove(m_metaPath);
        }
        setState(State::Canceled);
        emit downloadFinished(false);
        return;
    }

    if (m_pauseRequested) {
        closeFile();
        setState(State::Paused);
        emit downloadFinished(false);
        return;
    }

    if (m_abortRequested || reply->error() != QNetworkReply::NoError) {
        closeFile();  // flush; keep .part/.meta for a later resume
        if (m_error.isEmpty())
            m_error = reply->errorString();
        setState(State::Failed);
        emit downloadFinished(false);
        return;
    }

    closeFile();
    setState(State::Verifying);
    verifySha256();
}

void DownloadTask::handleRedirect(const QUrl &target)
{
    if (m_redirectCount >= 5) {
        fail(QObject::tr("Too many redirects"));
        emit downloadFinished(false);
        return;
    }
    m_redirectCount++;

    const QUrl next = m_effectiveUrl.resolved(target);
    if (!isAllowedUrl(next)) {
        fail(QObject::tr("Refusing to follow a non-https redirect"));
        emit downloadFinished(false);
        return;
    }

    // Drop the Authorization header when the host changes (ADR 45 / §7.3), so
    // a personal HF token never leaks to a CDN.
    if (next.host() != m_effectiveUrl.host())
        m_authorization.clear();

    m_effectiveUrl = next;
    issueRequest();
}

bool DownloadTask::isAllowedUrl(const QUrl &url) const
{
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0)
        return true;
    if (!m_allowLoopbackHttp)
        return false;
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0)
        return false;
    const QString host = url.host();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")
           || host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
}

// ---------------------------------------------------------------------------
// Verification → rename / metadata
// ---------------------------------------------------------------------------

void DownloadTask::verifySha256()
{
    const QByteArray digest = m_hash.result().toHex();
    const QString expected = m_request.sha256.trimmed().toLower();
    if (!expected.isEmpty() && QString::fromLatin1(digest) != expected) {
        QFile::remove(m_partPath);
        QFile::remove(m_metaPath);
        m_error = QObject::tr("Checksum mismatch for %1").arg(m_fileName);
        setState(State::Failed);
        emit downloadFinished(false);
        return;
    }

    // Atomic promotion: the .part already lives on the target volume (§3.1).
    // POSIX rename replaces the destination atomically, so the previous valid
    // file survives a failure; only fall back to remove-then-rename when the
    // target exists and rename refuses (e.g. Windows).
    if (!QFile::rename(m_partPath, m_finalPath)) {
        if (!QFileInfo::exists(m_finalPath)) {
            m_error = QObject::tr("Unable to finalize %1").arg(m_fileName);
            setState(State::Failed);
            emit downloadFinished(false);
            return;
        }
        QFile::remove(m_finalPath);
        if (!QFile::rename(m_partPath, m_finalPath)) {
            m_error = QObject::tr("Unable to finalize %1").arg(m_fileName);
            setState(State::Failed);
            emit downloadFinished(false);
            return;
        }
    }
    QFile::remove(m_metaPath);

    m_totalBytes = m_receivedBytes;
    m_error.clear();
    setState(State::Completed);
    emit downloadFinished(true);
}

void DownloadTask::closeFile()
{
    if (m_file.isOpen()) {
        m_file.flush();
        flushToDisk(m_file);
        m_file.close();
    }
    m_fileOpen = false;
}

// ---------------------------------------------------------------------------
// Resume metadata (JSON, QSaveFile, schema-versioned — §7.2)
// ---------------------------------------------------------------------------

void DownloadTask::persistMeta()
{
    QJsonObject o;
    o.insert(QStringLiteral("schemaVersion"), 1);
    o.insert(QStringLiteral("etag"), m_resumeEtag);
    o.insert(QStringLiteral("lastModified"), m_resumeLastModified);
    if (m_totalBytes >= 0)
        o.insert(QStringLiteral("total"), double(m_totalBytes));

    QSaveFile f(m_metaPath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QTextStream out(&f);
    out << QJsonDocument(o).toJson(QJsonDocument::Compact);
    out.flush();
    f.commit();
}

void DownloadTask::readMeta()
{
    QFile f(m_metaPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;
    const QJsonObject o = doc.object();
    m_resumeEtag = o.value(QStringLiteral("etag")).toString();
    m_resumeLastModified = o.value(QStringLiteral("lastModified")).toString();
    const double total = o.value(QStringLiteral("total")).toDouble(-1);
    m_resumeExpectedTotal = total < 0 ? -1 : static_cast<qint64>(total);
}

// ---------------------------------------------------------------------------
// Control verbs
// ---------------------------------------------------------------------------

void DownloadTask::pause()
{
    if (m_state != State::Running)
        return;
    m_pauseRequested = true;
    if (m_reply)
        m_reply->abort();
}

void DownloadTask::cancel(bool deletePartial)
{
    if (m_state == State::Completed || m_state == State::Canceled)
        return;
    m_cancelRequested = true;
    m_cancelDeletePartial = deletePartial;
    if (m_reply) {
        m_reply->abort();
    } else {
        // Not started: nothing on disk beyond a prior partial when requested.
        if (m_cancelDeletePartial) {
            QFile::remove(m_partPath);
            QFile::remove(m_metaPath);
        }
        setState(State::Canceled);
        emit downloadFinished(false);
    }
}

void DownloadTask::abortDownload()
{
    if (m_state != State::Running && m_state != State::Queued)
        return;
    m_abortRequested = true;
    if (m_reply) {
        m_reply->abort();
    } else {
        closeFile();
        m_error = QObject::tr("Download interrupted");
        setState(State::Failed);
        emit downloadFinished(false);
    }
}

}  // namespace llocr