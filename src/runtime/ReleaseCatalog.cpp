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
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include "runtime/ReleaseCatalog.h"

namespace llocr {

namespace {

const QRegularExpression kGenericRe(
    QStringLiteral(R"(^llama-b(\d+)-bin-([a-z0-9]+)-([a-z0-9]+)\.(zip|tar\.gz)$)"));

const QRegularExpression kBackendRe(
    QStringLiteral(R"(^llama-b(\d+)-bin-([a-z0-9]+)-(.+)-([a-z0-9]+)\.(zip|tar\.gz)$)"));

const QRegularExpression kCudartRe(
    QStringLiteral(R"(^cudart-llama-bin-win-cuda-([0-9a-z.]+?)(?:-(x64|arm64))?\.zip$)"));

const QRegularExpression kShaRe(
    QStringLiteral(R"((?:sha256\s*[:=]\s*)?([0-9a-fA-F]{64})\s+(\S+))"));

QString normalizedLower(const QString &s) { return s.toLower(); }

}  // namespace

int extractBuildNumberFromTag(const QString &tagName)
{
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
    const QRegularExpressionMatch gm = kGenericRe.match(fileName);
    if (gm.hasMatch()) {
        a.os = gm.captured(2);
        a.arch = gm.captured(3);
        a.build = QStringLiteral("b%1").arg(gm.captured(1));
    } else {
        const QRegularExpressionMatch bm = kBackendRe.match(fileName);
        if (bm.hasMatch()) {
            a.os = bm.captured(2);
            a.backend = bm.captured(3);
            a.arch = bm.captured(4);
            a.build = QStringLiteral("b%1").arg(bm.captured(1));
        }
    }
    if (a.os == QStringLiteral("ubuntu"))
        a.os = QStringLiteral("linux");
    return a;
}

QHash<QString, QString> ReleaseCatalog::parseSha256Table(const QString &body)
{
    QHash<QString, QString> out;
    if (body.trimmed().isEmpty())
        return out;
    QRegularExpressionMatchIterator it = kShaRe.globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.lastCapturedIndex() < 2)
            continue;
        const QString digest = m.captured(1).toLower();
        QString name = m.captured(2).trimmed();
        if (name.isEmpty())
            continue;
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

QList<ReleaseInfo> ReleaseCatalog::readCacheFile(const QString &cacheDir, QString &error,
                                                qint64 *cachedBuild)
{
    const QString path = QDir(cacheDir).filePath(QStringLiteral("releases.json"));
    if (!QFileInfo::exists(path)) {
        error = QObject::tr("No cached releases yet");
        return QList<ReleaseInfo>();
    }
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
    if (cachedBuild && !arr.isEmpty()) {
        *cachedBuild = extractBuildNumberFromTag(
            arr.first().toObject().value(QStringLiteral("tag_name")).toString());
    }
    return parseReleasesJson(arr, error);
}

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
    return readCacheFile(cacheDir, error, &cachedBuild);
}

QList<ReleaseInfo> ReleaseCatalog::fetchReleasesLocal(QNetworkAccessManager *nam,
                                                      QString cacheDir, QString &error,
                                                      int timeoutMs, const QString &apiUrl)
{
    QList<ReleaseInfo> fromCache;
    QDateTime cachedAt;
    qint64 cachedBuild = -1;
    bool isFresh = false;
    QString cacheErr;
    fromCache = loadCache(cacheDir, cachedAt, cachedBuild, isFresh, cacheErr);
    if (isFresh)
        return fromCache;
    // Age alone does not invalidate the list: keep the last known good one as
    // the offline fallback for a failed fetch.
    if (fromCache.isEmpty())
        fromCache = readCacheFile(cacheDir, cacheErr, &cachedBuild);

    if (!nam) {
        error = QObject::tr("No network client available");
        return QList<ReleaseInfo>();
    }

    QNetworkRequest request(
        QUrl(apiUrl.isEmpty() ? QLatin1String(kApiUrl) : apiUrl));
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/vnd.github+json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LLocr/0.2.0").toUtf8());
    request.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = nam->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // setTransferTimeout() bounds the transfer, but the nested loop must never
    // outlive it: a stalled connect would otherwise hang the installer.
    bool timedOut = false;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    guard.start(timeoutMs + 5000);
    loop.exec();

    // A transport failure, a rate limit or an unexpected body must NOT drop the
    // cached list: it is the only offline fallback, and the cache is replaced
    // exclusively by a successfully parsed list (see the QSaveFile below). Fall
    // back to the stale list instead of reporting "no releases available".
    const auto failWithCache = [&fromCache](const QString &message) {
        return fromCache.isEmpty() ? QList<ReleaseInfo>() : fromCache;
    };

    if (timedOut || reply->error() == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        error = QObject::tr("Timed out fetching release list");
        return failWithCache(error);
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    const QString transportError =
        reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
    // Read before deleteLater(): a queued delete must not be reached first.
    const qint64 resetEpoch =
        reply->rawHeader(QByteArrayLiteral("X-RateLimit-Reset")).toLongLong();
    reply->deleteLater();

    if (status == 403) {
        QString when = QObject::tr("soon");
        if (resetEpoch > 0) {
            when = QDateTime::fromSecsSinceEpoch(resetEpoch)
                       .toLocalTime()
                       .toString(Qt::ISODate);
        }
        error = QObject::tr("GitHub rate limit reached; retry around %1").arg(when);
        return failWithCache(error);
    }
    if (status != 200) {
        error = status > 0
            ? QObject::tr("GitHub API returned HTTP %1").arg(status)
            : QObject::tr("GitHub request failed: %1").arg(transportError);
        return failWithCache(error);
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        error = QObject::tr("Malformed release list from GitHub");
        return failWithCache(error);
    }
    const QList<ReleaseInfo> parsed = parseReleasesJson(doc.array(), error);
    if (parsed.isEmpty() && error.isEmpty())
        error = QObject::tr("No releases parsed");

    QDir().mkpath(cacheDir);
    QSaveFile sf(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(doc.array()).toJson(QJsonDocument::Compact));
        sf.commit();
    }
    return parsed;
}

PlatformInfo ReleaseCatalog::detectPlatform()
{
    PlatformInfo info;
    const QString kernel = QSysInfo::kernelType().toLower();
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();

    info.arch = QStringLiteral("x64");  // safe default for unknown arches
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64"))
        info.arch = QStringLiteral("x64");
    else if (arch == QLatin1String("i386") || arch == QLatin1String("i586")
             || arch == QLatin1String("i686") || arch == QLatin1String("x86"))
        info.arch = QStringLiteral("x86");
    else if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64"))
        info.arch = QStringLiteral("arm64");
    if (kernel.startsWith(QLatin1String("win"))) {
        info.os = PlatformOs::Windows;
        info.osTag = QStringLiteral("win");
        info.backend = QStringLiteral("cpu");
        info.backendReason = QObject::tr("CPU backend by default; enable CUDA if an NVIDIA GPU is present");
    } else if (kernel.contains(QLatin1String("mac")) || kernel.contains(QLatin1String("darwin"))) {
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