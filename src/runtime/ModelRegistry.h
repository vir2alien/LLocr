#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace llocr {

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
    QString dir;         // containing directory
    ModelOrigin origin = ModelOrigin::External;
    qint64 byteSize = 0;
    QString quantization;
    QString license;
    QString sha256;      // digest of the main file (from lfs.oid / preset)
    QString parser;
    QString prompt;
    int ctxSize = 8192;
    bool ctxSizeSet = false;
    QString addedAt;     // ISO timestamp
    QString repoId;
};

class ModelRegistry
{
public:
    static QString indexPathFor(const QString &modelsDir);
    static QString lockPathFor(const QString &modelsDir);

    static QList<ModelEntry> load(const QString &modelsDir, bool &rebuilt,
                                  QString &error);

    static bool save(const QString &modelsDir, const QList<ModelEntry> &entries,
                     QString &error);

    static QList<ModelEntry> scanModelsDir(const QString &modelsDir);

    static QString removalError(const ModelEntry &e, const QString &modelsDir,
                                bool active, bool runtimeReady);

    static QString canonicalPath(const QString &p);

public:
    static constexpr int kSchemaVersion = 1;
};

}  // namespace llocr