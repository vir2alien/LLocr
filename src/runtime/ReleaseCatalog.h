#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "runtime/ReleaseAsset.h"

class QNetworkAccessManager;

namespace llocr {

// Detected host platform used to pick a release asset (§ Stage D task 2).
enum class PlatformOs { Windows, Linux, macOS };

struct PlatformInfo {
    PlatformOs os = PlatformOs::Linux;
    QString osTag;     // "win" | "linux" | "macos" (asset-name token)
    QString arch;      // "x64" | "arm64" — expected asset-name token
    // Recommended backend is only a highlight; the user always chooses (§5).
    QString backend;   // "cpu" | "cuda" | "metal" | "vulkan" | ...
    QString backendReason;
};

// Queries the GitHub Releases API for llama.cpp, caches the manifest locally
// with a TTL, parses asset names into (os, backend, arch) and extracts the
// per-file sha256 digests published in the release body. Also performs
// platform detection and backend recommendation for the setup UI.
//
// Parsing helpers are static and operate on raw JSON/text so they can be unit
// tested against saved metadata of real releases without network (see
// test_release_catalog). Only fetchLatestLocal() touches the network, and only
// when the cache has expired.
class ReleaseCatalog
{
public:
    static constexpr qint64 kCacheTtlMs = 6LL * 60 * 60 * 1000;   // 6 h
    static constexpr int kRequestTimeoutMs = 20000;
    static constexpr const char *kApiUrl =
        "https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=10";

    // --- Platform detection (offline) ---------------------------------------
    static PlatformInfo detectPlatform();

    // --- Parsing (offline, unit-testable) -----------------------------------
    /// Parses a GitHub releases `items` JSON array into ReleaseInfo records.
    /// Tolerant: unknown asset names keep os/backend/arch empty and remain in
    /// the asset list (shown only in advanced mode). Returns empty on error and
    /// sets `error`.
    static QList<ReleaseInfo> parseReleasesJson(const QJsonArray &items,
                                                QString &error);

    /// Parses a single asset file name into os/backend/arch (+cudart / prefix
    /// detection). Non-matching names yield empty fields rather than a failure.
    static ReleaseAsset parseAssetName(const QString &fileName,
                                       const QString &downloadUrl, qint64 size);

    /// Scans a release body for `sha256: <hex>  <filename>` blocks (and the
    /// plain `64hex <filename>` form) and returns a map from file name
    /// (lowercased) to digest. Tolerant to layout variations.
    static QHash<QString, QString> parseSha256Table(const QString &body);

    // --- Local cache (offline) ---------------------------------------------
    /// Reads + parses <cacheDir>/releases.json, returning cached releases, or
    /// empty when missing/younger-TTL logic mismatched. `cachedBuild` and
    /// `cachedAt` report what is stored.
    static QList<ReleaseInfo> loadCache(const QString &cacheDir,
                                        QDateTime &cachedAt,
                                        qint64 &cachedBuild,
                                        bool &isFresh,
                                        QString &error);

    /// Marks an existing cache file as expired (call after a failed fetch so
    /// the next launch retries instead of serving stale data), or removes it.
    static void resetCache(const QString &cacheDir);

    // --- Network entry point ----------------------------------------------
    /// Fetches https://api.github.com/.../releases?per_page=10 with an
    /// Llama-API User-Agent, caches a trimmed manifest under
    /// <cacheDir>/releases.json, and re-reads it. On 403 it surfaces the
    /// X-RateLimit-Reset timestamp. Returns the manifest (possibly from a
    /// fresh cache) or an empty list; `error` explains a failure.
    static QList<ReleaseInfo> fetchReleasesLocal(QNetworkAccessManager *nam,
                                                 QString cacheDir,
                                                 QString &error,
                                                 int timeoutMs = kRequestTimeoutMs);
};

// Parse a numeric build from a "b10594" tag, or -1.
int extractBuildNumberFromTag(const QString &tagName);

}  // namespace llocr