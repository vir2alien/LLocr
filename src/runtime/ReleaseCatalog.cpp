#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include "runtime/ReleaseCatalog.h"

namespace llocr {

namespace {

// Tolerant asset-name parser (§ Stage D task 1). Matches the llama.cpp layout:
//
//   llama-b<build>-bin-<os>-<backend>-<arch>.zip
//   cudart-llama-bin-win-cuda-<ver>.zip
//
// The backend token may itself contain dashes (e.g. "cuda-cu12"), so the regex
// anchors os and arch at the boundaries and lets backend span the middle.
const QRegularExpression kMainRe(
    QStringLiteral(R"(^llama-b(\d+)-bin-(win|linux|macos)-(.+)-(\w+)\.zip$)"));

const QRegularExpression kCudartRe(
    QStringLiteral(R"(^cudart-llama-bin-win-cuda-([0-9a-z]+)\.zip$)"));

// Body entries: 64 hex chars followed by a file name, with an optional
// "sha256:" prefix. Matches across the common llama.cpp "### sha256" blocks.
const QRegularExpression kShaRe(
    QStringLiteral(R"((?:sha256\s*[:=]\s*)?([0-9a-fA-F]{64})\s+(\S+))"));

QString normalizedLower(const QString &s) { return s.toLower(); }

}  // namespace

int extractBuildNumberFromTag(const QString &tagName)
{
    // Accepts "b10594", "10594", "v10594".
    QString t = tagName.toLower();
    if (t.startsWith(QLatin1String("b")))
        t = t.mid(1);
    else if (t.startsWith(QLatin1String("v")))
        t = t.mid(1);
    bool ok = false;
    const qint64 n = t.toLongLong(&ok, 10);
    return ok ? int(n) : -1;
}

ReleaseAsset ReleaseCatalog::parseAssetName(const QString &fileName,
                                            const QString &downloadUrl, qint64 size)
{
    ReleaseAsset a;
    a.fileName = fileName;
    a.downloadUrl = downloadUrl;
    a.size = size;
    const QRegularExpressionMatch m = kCudartRe.match(fileName);
    if (m.hasMatch()) {
        a.cudart = true;
        a.os = QStringLiteral("win");
        a.backend = QStringLiteral("cuda");
        a.arch = QStringLiteral("x64");
        return a;
    }
    const QRegularExpressionMatch nm = kMainRe.match(fileName);
    if (nm.hasMatch()) {
        a.os = nm.captured(2);
        a.backend = nm.captured(3);
        a.arch = nm.captured(4);
        a.build = QStringLiteral("b%1").arg(nm.captured(1));
    }
    // The trailing build may be a stable hash (e.g. "b10594-4f2a") or a bare build.
    return a;
}

QHash<QString, QString> ReleaseCatalog::parseSha256Table(const QString &body)
{
    QHash<QString, QString> out;
    if (body.trimmed().isEmpty())
        return out;

    // Fallback: line-based scan for blocks like:
    //   sha256: d3b1...  llama-b1234-bin-win-cuda-x64.7z
    // Also handle the bare "hex  filename" form that some older bodies use.
    QRegularExpressionMatchIterator it = kShaRe.globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.lastCapturedIndex() < 2)
            continue;
        const QString digest = m.captured(1).toLower();
        QString name = m.captured(2).trimmed();
        if (name.isEmpty())
            continue;
        // Keep only the file token, ignoring any trailing hash sum column.
        name = name.section(QLatin1Char(' '), 0, 0);
        out.insert(normalizedLower(name), digest);
    }
    return out;
}

QList<ReleaseInfo> ReleaseCatalog::parseReleasesJson(const QJsonArray &items,
                                                     QString &error)
{
    QList<ReleaseInfo> releases;
    for (const QJsonValue &v : items) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        ReleaseInfo r;
        r.tagName = o.value(QStringLiteral("tag_name")).toString();
        r.name = o.value(QStringLiteral("name")).toString();
        r.publishedAt = o.value(QStringLiteral("published_at")).toString();
        r.build = extractBuildNumberFromTag(r.tagName);

        const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &av : assets) {
            if (!av.isObject())
                continue;
            const QJsonObject ao = av.toObject();
            const QString name = ao.value(QStringLiteral("name")).toString();
            const QString url = ao.value(QStringLiteral("browser_download_url")).toString();
            const qint64 sz =
                static_cast<qint64>(ao.value(QStringLiteral("size")).toDouble(-1));
            r.assets.append(parseAssetName(name, url, sz));
        }

        // Attach sha256 digests published in the body to matching assets.
        const QString body = o.value(QStringLiteral("body")).toString();
        r.body = body;
        const QHash<QString, QString> table = parseSha256Table(body);
        for (ReleaseAsset &asset : r.assets) {
            if (asset.sha256.isEmpty()) {
                const QString digest =
                    table.value(normalizedLower(asset.fileName));
                if (!digest.isEmpty())
                    asset.sha256 = digest;
            }
        }
        releases.append(r);
    }
    if (releases.isEmpty() && !items.isEmpty())
        error = QObject::tr("The GitHub releases response contained no releases");
    return releases;
}

// ---------------------------------------------------------------------------
// Local cache
// ---------------------------------------------------------------------------

QList<ReleaseInfo> ReleaseCatalog::loadCache(const QString &cacheDir,
                                             QDateTime &cachedAt,
                                             qint64 &cachedBuild,
                                             bool &isFresh, QString &error)
{
    isFresh = false;
    cachedBuild = -1;
    const QString path = QDir(cacheDir).filePath(QStringLiteral("releases.json"));
    QFileInfo fi(path);
    if (!fi.exists()) {
        error = QObject::tr("No cached releases yet");
        return QList<ReleaseInfo>();
    }
    cachedAt = fi.lastModified();
    if (cachedAt.isValid() && cachedAt.msecsTo(QDateTime::currentDateTimeUtc()) > kCacheTtlMs) {
        error = QObject::tr("Cached release list is stale");
        return QList<ReleaseInfo>();
    }
    isFresh = true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Unable to read cached releases");
        return QList<ReleaseInfo>();
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        error = QObject::tr("Cached releases are malformed");
        return QList<ReleaseInfo>();
    }
    const QJsonArray arr = doc.array();
    if (!arr.isEmpty()) {
        const QJsonObject first = arr.first().toObject();
        cachedBuild = extractBuildNumberFromTag(
            first.value(QStringLiteral("tag_name")).toString());
    }
    return parseReleasesJson(arr, error);
}

void ReleaseCatalog::resetCache(const QString &cacheDir)
{
    const QString path = QDir(cacheDir).filePath(QStringLiteral("releases.json"));
    QFile f(path);
    if (f.exists())
        f.remove();
}

// ---------------------------------------------------------------------------
// Network fetch
// ---------------------------------------------------------------------------

QList<ReleaseInfo> ReleaseCatalog::fetchReleasesLocal(QNetworkAccessManager *nam,
                                                      QString cacheDir, QString &error,
                                                      int timeoutMs)
{
    QList<ReleaseInfo> fromCache;
    QDateTime cachedAt;
    qint64 cachedBuild = -1;
    bool isFresh = false;
    QString cacheErr;
    fromCache = loadCache(cacheDir, cachedAt, cachedBuild, isFresh, cacheErr);
    if (isFresh)
        return fromCache;

    if (!nam) {
        error = QObject::tr("No network client available");
        return QList<ReleaseInfo>();
    }

    // Build the request with the GitHub API media type and a descriptive UA.
    QNetworkRequest request((QUrl(QLatin1String(kApiUrl))));
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/vnd.github+json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LLocr/0.2.0").toUtf8());

    QNetworkReply *reply = nam->get(request);
    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(&timer, &QTimer::timeout, reply,
                     [&]() { timedOut = true; reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (timedOut) {
        error = QObject::tr("Timed out fetching release list");
        resetCache(cacheDir);
        return QList<ReleaseInfo>();
    }
    timer.stop();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (status == 403) {
        // GitHub rate limit for anonymous clients; surface the reset time.
        const QByteArray resetRaw =
            reply->rawHeader(QByteArrayLiteral("X-RateLimit-Reset"));
        qint64 resetEpoch = resetRaw.toLongLong();
        QString when = QObject::tr("soon");
        if (resetEpoch > 0) {
            when = QDateTime::fromSecsSinceEpoch(resetEpoch)
                       .toLocalTime()
                       .toString(Qt::ISODate);
        }
        error = QObject::tr("GitHub rate limit reached; retry around %1").arg(when);
        resetCache(cacheDir);
        return QList<ReleaseInfo>();
    }
    if (status != 200) {
        error = QObject::tr("GitHub API returned HTTP %1").arg(status);
        resetCache(cacheDir);
        return QList<ReleaseInfo>();
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        error = QObject::tr("Malformed release list from GitHub");
        resetCache(cacheDir);
        return QList<ReleaseInfo>();
    }
    const QList<ReleaseInfo> parsed = parseReleasesJson(doc.array(), error);
    if (parsed.isEmpty() && error.isEmpty())
        error = QObject::tr("No releases parsed");

    // Persist the raw items array (GitHub shape) so loadCache() re-parses it
    // identically on a later run. Bodies are kept: they hold the sha256 table.
    QDir().mkpath(cacheDir);
    QSaveFile sf(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(doc.array()).toJson(QJsonDocument::Compact));
        sf.commit();
    }
    return parsed;
}

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

PlatformInfo ReleaseCatalog::detectPlatform()
{
    PlatformInfo info;
    const QString kernel = QSysInfo::kernelType().toLower();
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();

    info.arch = arch.contains(QLatin1String("64")) ? QStringLiteral("x64")
                                                    : QStringLiteral("arm64");
    if (arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch64")))
        info.arch = QStringLiteral("arm64");
    if (kernel.contains(QLatin1String("win"))) {
        info.os = PlatformOs::Windows;
        info.osTag = QStringLiteral("win");
        info.backend = QStringLiteral("cpu");
        info.backendReason = QObject::tr("CPU backend by default; enable CUDA if an NVIDIA GPU is present");
    } else if (kernel.contains(QLatin1String("mac"))) {
        info.os = PlatformOs::macOS;
        info.osTag = QStringLiteral("macos");
        info.backend = QStringLiteral("metal");
        info.backendReason =
            QStringLiteral("Metal backend recommended on Apple silicon; fall back to CPU on Intel");
    } else {
        info.os = PlatformOs::Linux;
        info.osTag = QStringLiteral("linux");
        info.backend = QStringLiteral("cpu");
        info.backendReason = QStringLiteral("CPU backend; Vulkan selected if a Vulkan loader is present");
    }
    return info;
}

}  // namespace llocr