#pragma once

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
    QString osTag;     // "win" | "linux" | "macos" (asset-name token)
    QString arch;      // "x64" | "arm64" — expected asset-name token
    QString backend;   // "cpu" | "cuda" | "metal" | "vulkan" | ...
    QString backendReason;
};

class ReleaseCatalog
{
public:
    static constexpr qint64 kCacheTtlMs = 6LL * 60 * 60 * 1000;   // 6 h
    static constexpr int kRequestTimeoutMs = 20000;
    static constexpr const char *kApiUrl =
        "https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=10";

    static PlatformInfo detectPlatform();
    static QList<ReleaseInfo> parseReleasesJson(const QJsonArray &items, QString &error);

    static ReleaseAsset parseAssetName(const QString &fileName,
                                       const QString &downloadUrl, qint64 size);
    static QHash<QString, QString> parseSha256Table(const QString &body);
    static QList<ReleaseInfo> loadCache(const QString &cacheDir,
                                        QDateTime &cachedAt,
                                        qint64 &cachedBuild,
                                        bool &isFresh,
                                        QString &error);
    static void resetCache(const QString &cacheDir);
    static QList<ReleaseInfo> fetchReleasesLocal(QNetworkAccessManager *nam,
                                                 QString cacheDir,
                                                 QString &error,
                                                 int timeoutMs = kRequestTimeoutMs);
};

int extractBuildNumberFromTag(const QString &tagName);

}  // namespace llocr