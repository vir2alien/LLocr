#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace llocr {

// One file entry returned by the Hugging Face tree API for a repo revision:
//
//   GET https://huggingface.co/api/models/{repo}/tree/{revision}?recursive=true
//
// `lfs.oid` is a sha256 digest (ADR 29) — the only strong integrity check we
// have for a model file; non-LFS files carry no digest and are flagged so the
// UI can warn the user (§ Stage E task 1).
struct HfFile {
    QString path;       // repo-relative path, may contain '/'
    QString name;       // last path segment
    qint64 size = 0;
    QString lfsOid;     // sha256 (lowercase hex), empty for non-LFS files
    bool isLfs = false;
    bool isDir = false;
    QString type;       // "file" | "directory" | "lfs" | ...
};

// A lightweight repo summary returned by the HF search endpoint:
//   GET /api/models?search={q}&filter=gguf&limit=30&sort=downloads
struct HfModelSummary {
    QString id;         // "org/repo"
    QString title;      // human title ("repoName")
    qint64 downloads = 0;
    qint64 likes = 0;
    QString license;    // SPDX id when present, else a URL, else empty
    bool gated = false; // true when the "gated" flag is set — show license link
    QString tags;       // comma-joined tags (e.g. "gguf,vision") for the UI
};

// The role of a single .gguf entry within a repo (ADR 43 / § Stage E task 1).
enum class ModelFileKind {
    NotModel,   // not a .gguf
    Model,      // main vision model (or one part of a multi-file split)
    Vision,     // mmproj projector
};

// Client for the Hugging Face model API (§ Stage E task 1). Parsing helpers are
// static and operate on raw JSON so they are unit-testable against saved tree
// fixtures without network. Only the fetchers touch QNetworkAccessManager, and
// they are written to run on a worker thread (QtConcurrent) so the UI stays
// responsive (mirrors ReleaseCatalog).
class ModelCatalog
{
public:
    static constexpr int kRequestTimeoutMs = 30000;
    static constexpr int kMaxRedirects = 5;

    // --- Offline parsing (unit-testable) -----------------------------------
    /// Parses a `recursive=true` tree `items` array into HfFile records.
    /// Tolerant of missing/dir/`lfs` objects; returns empty + `error` only when
    /// the array itself is unusable.
    static QList<HfFile> parseTreeJson(const QJsonArray &items, QString &error);

    /// Parses search `items` array into ModelSummary records.
    static QList<HfModelSummary> parseSearchJson(const QJsonArray &items);

    /// Extracts the "next" page URL from a `Link` response header (pagination).
    static QUrl nextPageUrl(const QByteArray &linkHeader);

    /// Repo-relative path → leaf name.
    static QString leafName(const QString &path);

    /// Filters a list of file names down to those ending in `.gguf`.
    static QStringList allGguf(const QStringList &names);

    /// Classifies a file name.
    static ModelFileKind fileKind(const QString &name);

    /// Matches GGUF split naming: "model-00001-of-00004.gguf" ->
    /// base="model", index=1, count=4. Returns false for non-split files.
    static bool splitMultiPart(const QString &name, QString *baseOut = nullptr,
                               int *indexOut = nullptr, int *countOut = nullptr);

    /// True when `name` is one part of a multi-part GGUF split.
    static bool isMultiPart(const QString &name);

    /// Extracts the quant token from a file name, e.g. "Q4_K_M", "F16", or an
    /// empty string when none is recognized.
    static QString quantizationFromName(const QString &name);

    /// Percent-encodes each path segment (V) so `repo`, `revision` and `path`
    /// tokens survive non-ASCII / reserved characters in the URL.
    static QString encodePath(const QString &path);

    /// Download (resolve) URL for a file at a pinned commit SHA:
    /// https://huggingface.co/{repo}/resolve/{commitSha}/{path}
    static QUrl resolveUrl(const QString &repo, const QString &commitSha,
                           const QString &path);

    // --- Network fetchers (worker-thread friendly) -------------------------
    /// Pins the repo's current commit SHA: GET /api/models/{repo} -> `sha`.
    /// Empty on failure; `error` explains. `authorization` (e.g.
    /// "Bearer hf_...") is optional and only sent to huggingface.co.
    static QString fetchHeadSha(QNetworkAccessManager *nam, const QString &repo,
                                QString &error, const QByteArray &authorization = {},
                                int timeoutMs = kRequestTimeoutMs);

    /// Fetches the recursive tree for a pinned revision, following pagination
    /// via the `Link: rel="next"` header. Empty on failure; `error` explains.
    static QList<HfFile> fetchTree(QNetworkAccessManager *nam, const QString &repo,
                                   const QString &commitSha, QString &error,
                                   const QByteArray &authorization = {},
                                   int timeoutMs = kRequestTimeoutMs);

    /// Search repositories: GET /api/models?search=...&filter=gguf&limit=30.
    static QList<HfModelSummary> search(QNetworkAccessManager *nam,
                                       const QString &query, QString &error,
                                       int limit = 30,
                                       const QByteArray &authorization = {},
                                       int timeoutMs = kRequestTimeoutMs);
};

}  // namespace llocr