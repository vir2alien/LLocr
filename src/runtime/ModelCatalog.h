#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace llocr {

struct HfFile {
    QString path;       // repo-relative path, may contain '/'
    QString name;       // last path segment
    qint64 size = 0;
    QString lfsOid;     // sha256 (lowercase hex), empty for non-LFS files
    bool isLfs = false;
    bool isDir = false;
    QString type;       // "file" | "directory" | "lfs" | ...
};

struct HfModelSummary {
    QString id;         // "org/repo"
    QString title;      // human title ("repoName")
    qint64 downloads = 0;
    qint64 likes = 0;
    QString license;    // SPDX id when present, else a URL, else empty
    bool gated = false; // true when the "gated" flag is set — show license link
    QString tags;       // comma-joined tags (e.g. "gguf,vision") for the UI
};

enum class ModelFileKind {
    NotModel,   // not a .gguf
    Model,      // main vision model (or one part of a multi-file split)
    Vision,     // mmproj projector
};

class ModelCatalog
{
public:
    static constexpr int kRequestTimeoutMs = 30000;
    static constexpr int kMaxRedirects = 5;

    static QList<HfFile> parseTreeJson(const QJsonArray &items, QString &error);
    static QList<HfModelSummary> parseSearchJson(const QJsonArray &items);
    static QUrl nextPageUrl(const QByteArray &linkHeader);
    static QString leafName(const QString &path);
    static QStringList allGguf(const QStringList &names);
    static ModelFileKind fileKind(const QString &name);
    static bool splitMultiPart(const QString &name, QString *baseOut = nullptr,
                               int *indexOut = nullptr, int *countOut = nullptr);
    static bool isMultiPart(const QString &name);

    static bool splitAscending(const QString &a, const QString &b);
    static QString quantizationFromName(const QString &name);

    static QString encodePath(const QString &path);

    static QUrl resolveUrl(const QString &repo, const QString &commitSha,
                           const QString &path);

    static QString fetchHeadSha(QNetworkAccessManager *nam, const QString &repo,
                                QString &error, const QByteArray &authorization = {},
                                int timeoutMs = kRequestTimeoutMs,
                                const QUrl &baseUrl = QUrl());

    static QList<HfFile> fetchTree(QNetworkAccessManager *nam, const QString &repo,
                                   const QString &commitSha, QString &error,
                                   const QByteArray &authorization = {},
                                   int timeoutMs = kRequestTimeoutMs,
                                   const QUrl &baseUrl = QUrl());

    static QList<HfModelSummary> search(QNetworkAccessManager *nam,
                                       const QString &query, QString &error,
                                       int limit = 30,
                                       const QByteArray &authorization = {},
                                       int timeoutMs = kRequestTimeoutMs);
};

}  // namespace llocr