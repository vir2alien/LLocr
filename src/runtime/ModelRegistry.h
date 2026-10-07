#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace llocr {

struct ReconcileResult;
struct ReconcileSelections;

enum class ModelOrigin {
    External,  // a user-provided GGUF somewhere outside modelsDir
    Managed,   // downloaded by LLocr into modelsDir/<org>__<repo>
};

struct ModelEntry {
    QString id;          // stable id (repo "org__repo" for managed)
    QString title;       // display name
    QString repo;        // HF repo id, empty for external
    QString revision;    // pinned commit SHA, empty for external
    QString modelPath;   // absolute path to the main .gguf
    QStringList parts;   // extra absolute part paths (multi-file split)
    QString mmprojPath;  // absolute path to the vision projector, or empty
    QString draftPath;   // absolute path to the speculative-decoding draft, or empty
    QString dir;         // containing directory
    ModelOrigin origin = ModelOrigin::External;
    qint64 byteSize = 0;
    QString quantization;
    QString license;
    QString sha256;  // digest of the main file (from lfs.oid / preset)
    QString parser;
    int ctxSize = 8192;
    bool ctxSizeSet = false;
    QString addedAt;  // ISO timestamp
    QString repoId;
    QStringList roles;  // "ocr" / "check"; empty = legacy entry (shown in both)

    bool operator==(const ModelEntry &other) const
    {
        return id == other.id && title == other.title && repo == other.repo && revision == other.revision && modelPath == other.modelPath && parts == other.parts && mmprojPath == other.mmprojPath &&
               draftPath == other.draftPath && dir == other.dir && origin == other.origin && byteSize == other.byteSize && quantization == other.quantization && license == other.license &&
               sha256 == other.sha256 && parser == other.parser && ctxSize == other.ctxSize && ctxSizeSet == other.ctxSizeSet && addedAt == other.addedAt && repoId == other.repoId &&
               roles == other.roles;
    }
    bool operator!=(const ModelEntry &other) const { return !(*this == other); }
};

class ModelRegistry
{
public:
    static QString indexPathFor(const QString &modelsDir);
    static QString lockPathFor(const QString &modelsDir);

    static QList<ModelEntry> load(const QString &modelsDir, bool &rebuilt, QString &error);
    static QList<ModelEntry> load(const QString &modelsDir, bool &rebuilt, QString &error, ReconcileResult *report, const ReconcileSelections &selections);

    static bool save(const QString &modelsDir, const QList<ModelEntry> &entries, QString &error);

    static bool update(const QString &modelsDir, const std::function<QList<ModelEntry>(QList<ModelEntry> &)> &mutate, QString &error);

private:
    static QList<ModelEntry> readIndex(const QString &modelsDir, QString &error);
    static bool writeIndex(const QString &modelsDir, const QList<ModelEntry> &entries, QString &error);

public:
    static QList<ModelEntry> scanModelsDir(const QString &modelsDir);

    static QString removalError(const ModelEntry &e, const QString &modelsDir, bool active, bool runtimeReady);

    static QString canonicalPath(const QString &p);

public:
    static constexpr int kSchemaVersion = 1;
};

}  // namespace llocr