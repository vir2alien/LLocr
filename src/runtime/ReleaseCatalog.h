#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

#include "runtime/ReleaseAsset.h"

class QNetworkAccessManager;

namespace llocr {

enum class PlatformOs { Windows, Linux, macOS };

struct PlatformInfo {
    PlatformOs os = PlatformOs::Linux;
    QString osTag;    // "win" | "linux" | "macos" (asset-name token)
    QString arch;     // "x64" | "arm64" — expected asset-name token
    QString backend;  // "cpu" | "cuda" | "metal" | "vulkan" | ...
    QString backendReason;
};

struct ReleaseFetch {
    QList<ReleaseInfo> releases;
    QString error;  // set: hard failure (releases empty) or the offline stable-cache serve (ADR 202, releases non-empty)
    QString note;   // set: the beta fallback — error empty, releases is the fresh beta list
};

class ReleaseCatalog
{
public:
    static constexpr qint64 kCacheTtlMs = 6LL * 60 * 60 * 1000;  // 6 h
    static constexpr int kRequestTimeoutMs = 20000;
    static constexpr const char *kApiUrl = "https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=10";
    static constexpr const char *kStableApiBase = "https://api.github.com/repos/ggml-org/llama.cpp";

    static PlatformInfo detectPlatform();

    static QString defaultBackendFor(const QString &osTag, const QString &arch);

    // The backends this platform can actually be built for — what the runtime
    // installer offers and what a profile may suggest as an alternative build.
    // macOS publishes one build per arch, so it has exactly one.
    static QStringList backendsFor(const PlatformInfo &info);

    static QList<ReleaseInfo> parseReleasesJson(const QJsonArray &items, QString &error);

    static ReleaseAsset parseAssetName(const QString &fileName, const QString &downloadUrl, qint64 size);
    static QHash<QString, QString> parseSha256Table(const QString &body);
    // `sourceUrl` binds the cache to the request it came from: a cache written
    // by another URL — or a legacy one without the mark — reads as absent and
    // can never become the offline fallback.
    static QList<ReleaseInfo> readCacheFile(const QString &cacheDir, const QString &sourceUrl, QString &error, qint64 *cachedBuild = nullptr);
    static QList<ReleaseInfo> loadCache(const QString &cacheDir, const QString &sourceUrl, QDateTime &cachedAt, qint64 &cachedBuild, bool &isFresh, QString &error);
    static QList<ReleaseInfo> fetchReleasesLocal(QNetworkAccessManager *nam, QString cacheDir, QString &error, int timeoutMs = kRequestTimeoutMs, const QString &apiUrl = QString());

    // The stable channel: resolves the stable tag's nightly-build pointer
    // (`nightly-tag.txt`), composes the installable release from the
    // pointed-at build and caches it in `stableCacheDir`. Failure comes back
    // as one of the ReleaseFetch shapes: hard failure (`error` set, `releases`
    // empty), the offline stable-cache serve (`error` set with the cached
    // list, the failWithCache mirror of ADR 202) or the beta fallback (`note`
    // set, `error` empty, fresh beta `releases`).
    static ReleaseFetch
    fetchStableReleaseLocal(QNetworkAccessManager *nam, const QString &stableCacheDir, const QString &betaCacheDir, int timeoutMs = kRequestTimeoutMs, const QString &apiBase = QString());

    static QString parseNightlyTag(const QString &content);
    static ReleaseInfo composeStableRelease(const ReleaseInfo &latest, const QString &pointerTag, const ReleaseInfo &resolved);
};

int extractBuildNumberFromTag(const QString &tagName);

}  // namespace llocr