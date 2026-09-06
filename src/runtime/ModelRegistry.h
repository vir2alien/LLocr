#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace llocr {

// Origin of a model registry entry (§ Stage E task 6).
enum class ModelOrigin {
    External,  // a user-provided GGUF somewhere outside modelsDir
    Managed,   // downloaded by LLocr into modelsDir/<org>__<repo>
};

// One installed model as recorded in <modelsDir>/index.json. A multi-part GGUF
// is a single entry: `modelPath` holds the first part (which is what llama.cpp
// expects in --model and auto-loads the others for), and `parts` lists any
// additional parts.
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
    // True when ctxSize was chosen explicitly at install time. Distinguishes a
    // real 8192 from "not specified" so 8192 is not overloaded as a sentinel
    // (review 2.8): only explicit ctx sizes are persisted in index.json.
    bool ctxSizeSet = false;
    QString addedAt;     // ISO timestamp
    // True HF repo id ("org/repo") persisted at install time. Older index.json
    // files lack it; scanModelsDir then falls back to the directory name.
    QString repoId;
};

class ModelRegistry
{
public:
    static constexpr int kSchemaVersion = 1;

    static QString indexPathFor(const QString &modelsDir);
    static QString lockPathFor(const QString &modelsDir);

    /// Reads <modelsDir>/index.json. On a missing/corrupt file it rescans
    /// `modelsDir` and rebuilds the index (`rebuilt` becomes true). Returns the
    /// entries.
    static QList<ModelEntry> load(const QString &modelsDir, bool &rebuilt,
                                  QString &error);

    /// Atomically writes the index (QSaveFile) under `.registry.lock`. Returns
    /// true on success.
    static bool save(const QString &modelsDir, const QList<ModelEntry> &entries,
                     QString &error);

    /// Scans `modelsDir` for *.gguf files (one entry per quant/model file
    /// within a repo subdirectory; split parts of one quant are merged) and
    /// reconstructs entries from what is on disk. Never removes an entry that a
    /// file confirms; used to rebuild after corruption.
    static QList<ModelEntry> scanModelsDir(const QString &modelsDir);

    /// Human-readable reason a model cannot be removed, or an empty string when
    /// removal is allowed. Applies the two hard rules from §5 task 6:
    /// only `managed` models, and only files inside `modelsDir` (canonical
    /// path check). `active`/`runtimeReady` enforce the "do not remove the
    /// active model while the server is Ready" guard in the UI layer.
    static QString removalError(const ModelEntry &e, const QString &modelsDir,
                                bool active, bool runtimeReady);

    /// Absolute, symlink-resolved path (QFileInfo::canonicalFilePath).
    static QString canonicalPath(const QString &p);
};

}  // namespace llocr