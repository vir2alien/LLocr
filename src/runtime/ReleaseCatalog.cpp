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

#include "runtime/HardwareInfo.h"
#include "runtime/HttpClient.h"

namespace llocr {

namespace {

const QRegularExpression kGenericRe(QStringLiteral(R"(^llama-b(\d+)-bin-([a-z0-9]+)-([a-z0-9]+)\.(zip|tar\.gz)$)"));

const QRegularExpression kBackendRe(QStringLiteral(R"(^llama-b(\d+)-bin-([a-z0-9]+)-(.+)-([a-z0-9]+)\.(zip|tar\.gz)$)"));

const QRegularExpression kCudartRe(QStringLiteral(R"(^cudart-llama-bin-win-cuda-([0-9a-z.]+?)(?:-(x64|arm64))?\.zip$)"));

const QRegularExpression kShaRe(QStringLiteral(R"((?:sha256\s*[:=]\s*)?([0-9a-fA-F]{64})\s+(\S+))"));

const QRegularExpression kPointerRe(QStringLiteral(R"(^b\d+$)"));

QString normalizedLower(const QString &s)
{
    return s.toLower();
}

// Upstream stopped publishing the sha256 table in the release body, and ADR 212
// refuses an unverifiable runtime; GitHub computes a digest per uploaded asset
// and returns it in the asset's "digest" field of the same API answer.
QString digestFromApiField(const QString &value)
{
    static const QRegularExpression kDigestRe(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
    const QRegularExpressionMatch m = kDigestRe.match(value.trimmed());
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

// The cache file is either the current `{"sourceUrl": …, "releases": […]}` or
// a legacy bare array, which carries no mark at all.
QJsonArray cachedReleasesArray(const QJsonDocument &doc, QString *sourceUrl)
{
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (sourceUrl)
            *sourceUrl = o.value(QStringLiteral("sourceUrl")).toString();
        return o.value(QStringLiteral("releases")).toArray();
    }
    if (sourceUrl)
        sourceUrl->clear();
    return doc.array();
}

// Plain http is accepted only on the loopback hosts a local mock server
// listens on; the production URLs are always https.
HttpClient::Response catalogGet(QNetworkAccessManager *nam, const QUrl &url, const HttpClient::Options &options)
{
    const QString host = url.host();
    const bool allowInsecure = host == QLatin1String("127.0.0.1") || host == QLatin1String("::1") || host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
    return HttpClient::get(nam, url, options, 5, allowInsecure);
}

QString responseFailure(const HttpClient::Response &response)
{
    if (response.timedOut)
        return QObject::tr("Timed out fetching release list");
    if (response.status != 200)
        return response.status > 0 ? QObject::tr("GitHub API returned HTTP %1").arg(response.status) : QObject::tr("GitHub request failed: %1").arg(response.error);
    return QString();
}

// /releases/latest and /releases/tags/<tag> answer a single object; the
// release parser speaks arrays.
ReleaseInfo parseSingleRelease(const QByteArray &payload, QJsonObject *raw, QString &reason)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        reason = QObject::tr("Malformed release list from GitHub");
        return {};
    }
    if (raw)
        *raw = doc.object();
    QJsonArray items;
    items.append(doc.object());
    QString parseError;
    const QList<ReleaseInfo> parsed = ReleaseCatalog::parseReleasesJson(items, parseError);
    if (parsed.isEmpty()) {
        reason = parseError.isEmpty() ? QObject::tr("No releases parsed") : parseError;
        return {};
    }
    return parsed.first();
}

QString nightlyPointerUrl(const ReleaseInfo &latest)
{
    for (const ReleaseAsset &asset : latest.assets) {
        if (asset.fileName == QLatin1String("nightly-tag.txt"))
            return asset.downloadUrl;
    }
    return {};
}

// The cached object must re-parse to the composed release: the stable tag is
// kept (the build comes back through the asset-name recovery), and only
// platform assets survive so the cached list matches the composed one.
void writeStableCache(const QString &cacheDir, const QString &sourceUrl, const QJsonObject &latest, const QJsonArray &resolvedAssets)
{
    QJsonArray platformAssets;
    for (const QJsonValue &v : resolvedAssets) {
        if (!v.isObject())
            continue;
        const QJsonObject ao = v.toObject();
        const ReleaseAsset parsed = ReleaseCatalog::parseAssetName(
            ao.value(QStringLiteral("name")).toString(), ao.value(QStringLiteral("browser_download_url")).toString(), static_cast<qint64>(ao.value(QStringLiteral("size")).toDouble(-1)));
        if (!parsed.os.isEmpty())
            platformAssets.append(v);
    }
    QJsonObject release = latest;
    release.insert(QStringLiteral("assets"), platformAssets);
    release.insert(QStringLiteral("prerelease"), false);
    QJsonArray releases;
    releases.append(release);
    QJsonObject cache;
    cache.insert(QStringLiteral("sourceUrl"), sourceUrl);
    cache.insert(QStringLiteral("releases"), releases);
    QDir().mkpath(cacheDir);
    QSaveFile sf(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

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

ReleaseAsset ReleaseCatalog::parseAssetName(const QString &fileName, const QString &downloadUrl, qint64 size)
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

QList<ReleaseInfo> ReleaseCatalog::parseReleasesJson(const QJsonArray &items, QString &error)
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
        r.prerelease = o.contains(QStringLiteral("prerelease")) ? o.value(QStringLiteral("prerelease")).toBool() : true;

        const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &av : assets) {
            if (!av.isObject())
                continue;
            const QJsonObject ao = av.toObject();
            ReleaseAsset asset = parseAssetName(
                ao.value(QStringLiteral("name")).toString(), ao.value(QStringLiteral("browser_download_url")).toString(), static_cast<qint64>(ao.value(QStringLiteral("size")).toDouble(-1)));
            asset.sha256 = digestFromApiField(ao.value(QStringLiteral("digest")).toString());
            r.assets.append(asset);
        }

        const QString body = o.value(QStringLiteral("body")).toString();
        r.body = body;
        const QHash<QString, QString> table = parseSha256Table(body);
        for (ReleaseAsset &asset : r.assets) {
            if (asset.sha256.isEmpty()) {
                const QString digest = table.value(normalizedLower(asset.fileName));
                if (!digest.isEmpty())
                    asset.sha256 = digest;
            }
        }
        // A stable tag ("v1.0.0") carries no build number; the asset names do,
        // and the install tag needs it.
        if (r.build < 0) {
            for (const ReleaseAsset &asset : r.assets) {
                const int fromAsset = extractBuildNumberFromTag(asset.build);
                if (fromAsset > 0) {
                    r.build = fromAsset;
                    break;
                }
            }
        }
        releases.append(r);
    }
    if (releases.isEmpty() && !items.isEmpty())
        error = QObject::tr("The GitHub releases response contained no releases");
    return releases;
}

QList<ReleaseInfo> ReleaseCatalog::readCacheFile(const QString &cacheDir, const QString &sourceUrl, QString &error, qint64 *cachedBuild)
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
    if (perr.error != QJsonParseError::NoError || !(doc.isArray() || doc.isObject())) {
        error = QObject::tr("Cached releases are malformed");
        return QList<ReleaseInfo>();
    }
    QString cachedFrom;
    const QJsonArray arr = cachedReleasesArray(doc, &cachedFrom);
    if (cachedFrom != sourceUrl || arr.isEmpty()) {
        error = QObject::tr("No cached releases yet");
        return QList<ReleaseInfo>();
    }
    if (cachedBuild) {
        *cachedBuild = extractBuildNumberFromTag(arr.first().toObject().value(QStringLiteral("tag_name")).toString());
    }
    return parseReleasesJson(arr, error);
}

QList<ReleaseInfo> ReleaseCatalog::loadCache(const QString &cacheDir, const QString &sourceUrl, QDateTime &cachedAt, qint64 &cachedBuild, bool &isFresh, QString &error)
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
    const QList<ReleaseInfo> list = readCacheFile(cacheDir, sourceUrl, error, &cachedBuild);
    if (list.isEmpty())
        return QList<ReleaseInfo>();
    isFresh = true;
    return list;
}

QList<ReleaseInfo> ReleaseCatalog::fetchReleasesLocal(QNetworkAccessManager *nam, QString cacheDir, QString &error, int timeoutMs, const QString &apiUrl)
{
    const QString requestUrl = apiUrl.isEmpty() ? QLatin1String(kApiUrl) : apiUrl;
    // A stable release carries no installable binaries, so it is noise to the
    // installer — and, topping the answer, it would blind the update check.
    const auto betaOnly = [](const QList<ReleaseInfo> &list) {
        QList<ReleaseInfo> out;
        for (const ReleaseInfo &r : list) {
            if (r.prerelease)
                out.append(r);
        }
        return out;
    };
    QList<ReleaseInfo> fromCache;
    QDateTime cachedAt;
    qint64 cachedBuild = -1;
    bool isFresh = false;
    QString cacheErr;
    fromCache = loadCache(cacheDir, requestUrl, cachedAt, cachedBuild, isFresh, cacheErr);
    if (isFresh)
        return betaOnly(fromCache);
    if (fromCache.isEmpty())
        fromCache = readCacheFile(cacheDir, requestUrl, cacheErr, &cachedBuild);

    if (!nam) {
        error = QObject::tr("No network client available");
        return QList<ReleaseInfo>();
    }

    HttpClient::Options options;
    options.timeoutMs = timeoutMs;
    options.headers = {
        {"Accept", "application/vnd.github+json"},
    };
    const HttpClient::Response response = catalogGet(nam, QUrl(requestUrl), options);
    const QByteArray payload = response.body;
    const int status = response.status;

    // Callers assign `error` first; the helper only picks cache-vs-empty.
    const auto failWithCache = [&fromCache, &betaOnly]() { return betaOnly(fromCache); };

    if (response.timedOut) {
        error = QObject::tr("Timed out fetching release list");
        return failWithCache();
    }

    qint64 resetEpoch = 0;
    for (const auto &header : response.headers) {
        if (header.first.compare(QByteArrayLiteral("X-RateLimit-Reset"), Qt::CaseInsensitive) == 0)
            resetEpoch = header.second.toLongLong();
    }
    const QString transportError = response.error;

    if (status == 403) {
        QString when = QObject::tr("soon");
        if (resetEpoch > 0) {
            when = QDateTime::fromSecsSinceEpoch(resetEpoch).toLocalTime().toString(Qt::ISODate);
        }
        error = QObject::tr("GitHub rate limit reached; retry around %1").arg(when);
        return failWithCache();
    }
    if (status != 200) {
        error = status > 0 ? QObject::tr("GitHub API returned HTTP %1").arg(status) : QObject::tr("GitHub request failed: %1").arg(transportError);
        return failWithCache();
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        error = QObject::tr("Malformed release list from GitHub");
        return failWithCache();
    }
    const QList<ReleaseInfo> parsed = betaOnly(parseReleasesJson(doc.array(), error));
    if (parsed.isEmpty() && error.isEmpty())
        error = QObject::tr("No releases parsed");

    QDir().mkpath(cacheDir);
    QSaveFile sf(QDir(cacheDir).filePath(QStringLiteral("releases.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        QJsonObject cache;
        cache.insert(QStringLiteral("sourceUrl"), requestUrl);
        cache.insert(QStringLiteral("releases"), doc.array());
        sf.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
        sf.commit();
    }
    return parsed;
}

QString ReleaseCatalog::parseNightlyTag(const QString &content)
{
    const QString trimmed = content.trimmed();
    return kPointerRe.match(trimmed).hasMatch() ? trimmed : QString();
}

ReleaseInfo ReleaseCatalog::composeStableRelease(const ReleaseInfo &latest, const QString &pointerTag, const ReleaseInfo &resolved)
{
    ReleaseInfo out;
    out.tagName = latest.tagName;
    out.name = latest.name;
    out.publishedAt = latest.publishedAt;
    out.build = extractBuildNumberFromTag(pointerTag);
    out.prerelease = false;
    out.body = resolved.body;
    for (const ReleaseAsset &asset : resolved.assets) {
        if (asset.os.isEmpty())
            continue;
        out.assets.append(asset);
    }
    return out;
}

ReleaseFetch ReleaseCatalog::fetchStableReleaseLocal(QNetworkAccessManager *nam, const QString &stableCacheDir, const QString &betaCacheDir, int timeoutMs, const QString &apiBase)
{
    const QString base = apiBase.isEmpty() ? QLatin1String(kStableApiBase) : apiBase;
    const QString latestUrl = base + QLatin1String("/releases/latest");

    QDateTime cachedAt;
    qint64 cachedBuild = -1;
    bool fresh = false;
    QString cacheErr;
    const QList<ReleaseInfo> cached = loadCache(stableCacheDir, latestUrl, cachedAt, cachedBuild, fresh, cacheErr);
    if (fresh)
        return {cached, {}, {}};
    const QList<ReleaseInfo> offline = readCacheFile(stableCacheDir, latestUrl, cacheErr);

    const QString betaUrl = apiBase.isEmpty() ? QString() : apiBase + QLatin1String("/releases?per_page=10");
    const auto hardFail = [&](const QString &reason) -> ReleaseFetch {
        if (!offline.isEmpty())
            return {offline, reason, {}};
        QString betaError;
        const QList<ReleaseInfo> beta = fetchReleasesLocal(nam, betaCacheDir, betaError, timeoutMs, betaUrl);
        if (beta.isEmpty())
            return {{}, betaError, {}};
        return {beta, {}, reason};
    };

    HttpClient::Options options;
    options.timeoutMs = timeoutMs;
    options.headers = {
        {"Accept", "application/vnd.github+json"},
    };

    QString reason;
    QJsonObject latestRaw;
    const HttpClient::Response latestResp = catalogGet(nam, QUrl(latestUrl), options);
    reason = responseFailure(latestResp);
    if (!reason.isEmpty())
        return hardFail(reason);
    const ReleaseInfo latest = parseSingleRelease(latestResp.body, &latestRaw, reason);
    if (!reason.isEmpty())
        return hardFail(reason);

    const QString pointerUrl = nightlyPointerUrl(latest);
    if (pointerUrl.isEmpty())
        return hardFail(QObject::tr("The stable release does not point at a build"));

    const HttpClient::Response pointerResp = catalogGet(nam, QUrl(pointerUrl), options);
    reason = responseFailure(pointerResp);
    if (!reason.isEmpty())
        return hardFail(reason);
    const QString pointerTag = parseNightlyTag(QString::fromUtf8(pointerResp.body));
    if (pointerTag.isEmpty())
        return hardFail(QObject::tr("The stable release does not point at a build"));

    QJsonObject resolvedRaw;
    const HttpClient::Response tagResp = catalogGet(nam, QUrl(base + QLatin1String("/releases/tags/") + pointerTag), options);
    reason = responseFailure(tagResp);
    if (!reason.isEmpty())
        return hardFail(reason);
    const ReleaseInfo resolved = parseSingleRelease(tagResp.body, &resolvedRaw, reason);
    if (!reason.isEmpty())
        return hardFail(reason);

    writeStableCache(stableCacheDir, latestUrl, latestRaw, resolvedRaw.value(QStringLiteral("assets")).toArray());
    return {{composeStableRelease(latest, pointerTag, resolved)}, {}, {}};
}

PlatformInfo ReleaseCatalog::detectPlatform()
{
    PlatformInfo info;
    const QString kernel = QSysInfo::kernelType().toLower();
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();

    info.arch = QStringLiteral("x64");  // safe default for unknown arches
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64"))
        info.arch = QStringLiteral("x64");
    else if (arch == QLatin1String("i386") || arch == QLatin1String("i586") || arch == QLatin1String("i686") || arch == QLatin1String("x86"))
        info.arch = QStringLiteral("x86");
    else if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64"))
        info.arch = QStringLiteral("arm64");
    if (kernel.startsWith(QLatin1String("win"))) {
        info.os = PlatformOs::Windows;
        info.osTag = QStringLiteral("win");
        const HardwareReport hardware = HardwareInfo::detect();
        info.backend = HardwareInfo::recommendedBackendFor(info.osTag, hardware.adapters);
        if (info.backend == QLatin1String("cuda"))
            info.backendReason = QObject::tr("NVIDIA GPU detected (%1): CUDA backend recommended").arg(hardware.adapters.first().name);
        else if (info.backend == QLatin1String("vulkan"))
            info.backendReason = QObject::tr("GPU detected (%1): Vulkan backend recommended").arg(hardware.adapters.first().name);
        else
            info.backendReason = QObject::tr("No GPU detected: CPU backend");
    } else if (kernel.contains(QLatin1String("mac")) || kernel.contains(QLatin1String("darwin"))) {
        info.os = PlatformOs::macOS;
        info.osTag = QStringLiteral("macos");
        info.backend = defaultBackendFor(info.osTag, info.arch);
        info.backendReason = QObject::tr("Metal backend recommended on Apple silicon; fall back to CPU on Intel");
    } else {
        info.os = PlatformOs::Linux;
        info.osTag = QStringLiteral("linux");
        info.backend = QStringLiteral("cpu");
        info.backendReason = QStringLiteral("CPU backend; Vulkan selected if a Vulkan loader is present");
    }
    return info;
}

QString ReleaseCatalog::defaultBackendFor(const QString &osTag, const QString &arch)
{
    return osTag == QLatin1String("macos") && arch == QLatin1String("arm64") ? QStringLiteral("metal") : QStringLiteral("cpu");
}

QStringList ReleaseCatalog::backendsFor(const PlatformInfo &info)
{
    switch (info.os) {
    case PlatformOs::Windows:
        return {QStringLiteral("cpu"), QStringLiteral("cuda"), QStringLiteral("vulkan")};
    case PlatformOs::macOS:
        // macOS publishes one build per arch, and it is the one the platform
        // defaults to: offering "cpu" there would offer a download the platform
        // has no artifact for — the same tarball under a second name.
        return {defaultBackendFor(info.osTag, info.arch)};
    case PlatformOs::Linux:
        return {QStringLiteral("cpu"), QStringLiteral("vulkan"), QStringLiteral("cuda")};
    }
    return {QStringLiteral("cpu"), QStringLiteral("vulkan"), QStringLiteral("cuda")};
}

}  // namespace llocr