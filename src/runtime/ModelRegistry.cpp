#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelRegistry.h"

namespace llocr {

namespace {

constexpr const char *kIndexFile = "index.json";
constexpr const char *kLockFile = ".registry.lock";
constexpr const char *kModelsKey = "models";

QString originToString(ModelOrigin o)
{
    return o == ModelOrigin::Managed ? QStringLiteral("managed")
                                     : QStringLiteral("external");
}

ModelOrigin originFromString(const QString &s)
{
    return s == QLatin1String("managed") ? ModelOrigin::Managed
                                         : ModelOrigin::External;
}

// True when `path` (an absolute canonical path) is inside `dir` (canonical).
bool isSubpathOf(const QString &path, const QString &dir)
{
    if (dir.isEmpty())
        return false;
    // Normalize to '/' before comparing: Qt path APIs may return a mix of
    // '/' and '\' on Windows while QDir::separator() is '\', which broke the
    // simple startsWith(dir + separator) check after the toolchain moved to
    // MSVC 2022 (it worked on MinGW/macOS where '/' is the native separator).
    const QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString d = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    if (d.isEmpty())
        return false;
    return p == d || p.startsWith(d + QLatin1Char('/'));
}

// Split names sort so the "…-00001-of-NNN" part is first.
void sortSplitParts(QStringList *names)
{
    std::sort(names->begin(), names->end(), &ModelCatalog::splitAscending);
}

// Grouping key shared by all files of one quant: "Unlimited-OCR-Q8_0.gguf" and
// "Unlimited-OCR-Q8_0-00001-of-00002.gguf" both map to "Unlimited-OCR-Q8_0".
QString modelStem(const QString &name)
{
    QString base;
    int idx = 0, count = 0;
    if (ModelCatalog::splitMultiPart(name, &base, &idx, &count))
        return base;
    QString n = name;
    if (n.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive))
        n.chop(5);
    return n;
}

ModelEntry entryFromJson(const QJsonObject &o)
{
    ModelEntry e;
    e.id = o.value(QStringLiteral("id")).toString();
    e.title = o.value(QStringLiteral("title")).toString();
    e.repo = o.value(QStringLiteral("repo")).toString();
    e.revision = o.value(QStringLiteral("revision")).toString();
    e.modelPath = o.value(QStringLiteral("modelPath")).toString();
    e.mmprojPath = o.value(QStringLiteral("mmprojPath")).toString();
    e.dir = o.value(QStringLiteral("dir")).toString();
    e.origin = originFromString(o.value(QStringLiteral("managed")).toString());
    e.byteSize =
        static_cast<qint64>(o.value(QStringLiteral("byteSize")).toDouble(0));
    e.quantization = o.value(QStringLiteral("quantization")).toString();
    e.license = o.value(QStringLiteral("license")).toString();
    e.sha256 = o.value(QStringLiteral("sha256")).toString();
    e.parser = o.value(QStringLiteral("parser")).toString();
    e.prompt = o.value(QStringLiteral("prompt")).toString();
    if (o.contains(QStringLiteral("ctxSize"))) {
        e.ctxSize = o.value(QStringLiteral("ctxSize")).toInt();
        e.ctxSizeSet = true;
    }
    e.addedAt = o.value(QStringLiteral("addedAt")).toString();
    e.repoId = o.value(QStringLiteral("repoId")).toString();
    const QJsonArray parts = o.value(QStringLiteral("parts")).toArray();
    for (const QJsonValue &v : parts)
        e.parts << v.toString();
    if (e.dir.isEmpty() && !e.modelPath.isEmpty())
        e.dir = QFileInfo(e.modelPath).absolutePath();
    return e;
}

QJsonObject entryToJson(const ModelEntry &e)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), e.id);
    if (!e.title.isEmpty())
        o.insert(QStringLiteral("title"), e.title);
    if (!e.repo.isEmpty())
        o.insert(QStringLiteral("repo"), e.repo);
    if (!e.revision.isEmpty())
        o.insert(QStringLiteral("revision"), e.revision);
    o.insert(QStringLiteral("modelPath"), e.modelPath);
    if (!e.mmprojPath.isEmpty())
        o.insert(QStringLiteral("mmprojPath"), e.mmprojPath);
    if (!e.dir.isEmpty())
        o.insert(QStringLiteral("dir"), e.dir);
    o.insert(QStringLiteral("managed"), originToString(e.origin));
    if (e.byteSize > 0)
        o.insert(QStringLiteral("byteSize"), static_cast<double>(e.byteSize));
    if (!e.quantization.isEmpty())
        o.insert(QStringLiteral("quantization"), e.quantization);
    if (!e.license.isEmpty())
        o.insert(QStringLiteral("license"), e.license);
    if (!e.sha256.isEmpty())
        o.insert(QStringLiteral("sha256"), e.sha256);
    if (!e.parser.isEmpty())
        o.insert(QStringLiteral("parser"), e.parser);
    if (!e.prompt.isEmpty())
        o.insert(QStringLiteral("prompt"), e.prompt);
    if (e.ctxSizeSet)
        o.insert(QStringLiteral("ctxSize"), e.ctxSize);
    o.insert(QStringLiteral("addedAt"), e.addedAt);
    if (!e.repoId.isEmpty())
        o.insert(QStringLiteral("repoId"), e.repoId);
    if (!e.parts.isEmpty()) {
        QJsonArray arr;
        for (const QString &p : e.parts)
            arr.append(p);
        o.insert(QStringLiteral("parts"), arr);
    }
    return o;
}

}  // namespace

QString ModelRegistry::indexPathFor(const QString &modelsDir)
{
    return QDir(modelsDir).filePath(QLatin1String(kIndexFile));
}

QString ModelRegistry::lockPathFor(const QString &modelsDir)
{
    return QDir(modelsDir).filePath(QLatin1String(kLockFile));
}

QList<ModelEntry> ModelRegistry::load(const QString &modelsDir, bool &rebuilt,
                                      QString &error)
{
    rebuilt = false;
    const QString path = indexPathFor(modelsDir);
    QFile f(path);
    if (!f.exists()) {
        rebuilt = true;
        const QList<ModelEntry> scanned = scanModelsDir(modelsDir);
        // Persist the rebuilt index so every launch does not re-scan (§3.13).
        QString saveErr;
        save(modelsDir, scanned, saveErr);
        return scanned;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Unable to read model registry: %1").arg(f.errorString());
        rebuilt = true;
        const QList<ModelEntry> scanned = scanModelsDir(modelsDir);
        QString saveErr;
        save(modelsDir, scanned, saveErr);
        return scanned;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QObject::tr("Model index is corrupt; rescanning models directory");
        rebuilt = true;
        const QList<ModelEntry> scanned = scanModelsDir(modelsDir);
        QString saveErr;
        save(modelsDir, scanned, saveErr);
        return scanned;
    }
    if (doc.object().value(QStringLiteral("schemaVersion")).toInt(-1)
        != kSchemaVersion) {
        error = QObject::tr("Model index version mismatch; rescanning");
        rebuilt = true;
        const QList<ModelEntry> scanned = scanModelsDir(modelsDir);
        QString saveErr;
        save(modelsDir, scanned, saveErr);
        return scanned;
    }
    const QJsonArray arr =
        doc.object().value(QLatin1String(kModelsKey)).toArray();
    QList<ModelEntry> out;
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        ModelEntry e = entryFromJson(v.toObject());
        if (!e.modelPath.isEmpty() || !e.dir.isEmpty())
            out.append(std::move(e));
    }

    // Multi-quant reconciliation: older indexes kept a single entry per repo
    // directory (first quant only) and separately-downloaded quants have no
    // row at all. Re-scan and append any on-disk model the index does not
    // already reference; existing rows keep their install-time metadata.
    for (const ModelEntry &s : scanModelsDir(modelsDir)) {
        if (s.modelPath.isEmpty())
            continue;
        const bool present = std::any_of(
            out.cbegin(), out.cend(), [&](const ModelEntry &x) {
                return !x.modelPath.isEmpty() && x.modelPath == s.modelPath;
            });
        if (!present)
            out.append(s);
    }
    return out;
}

bool ModelRegistry::save(const QString &modelsDir, const QList<ModelEntry> &entries,
                         QString &error)
{
    QDir().mkpath(modelsDir);

    // H.6 / ADR 46: guard the read-modify-write cycle on index.json with the
    // per-write registry lock so a second GUI instance cannot interleave and
    // lose updates. The atomic rename protects against corruption, not against
    // lost writes.
    QLockFile lock(lockPathFor(modelsDir));
    lock.setStaleLockTime(30 * 1000);
    if (!lock.tryLock(5000)) {
        error = QObject::tr("Model registry is locked by another LLocr instance");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    QJsonArray arr;
    for (const ModelEntry &e : entries)
        arr.append(entryToJson(e));
    root.insert(QLatin1String(kModelsKey), arr);

    QSaveFile f(indexPathFor(modelsDir));
    f.setDirectWriteFallback(true);
    if (!f.open(QIODevice::WriteOnly)) {
        error = QObject::tr("Unable to open model index for writing: %1")
                    .arg(f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        error = QObject::tr("Unable to commit model index: %1").arg(f.errorString());
        return false;
    }
    return true;
}

QList<ModelEntry> ModelRegistry::scanModelsDir(const QString &modelsDir)
{
    QList<ModelEntry> out;
    const QDir base(modelsDir);
    if (!base.exists())
        return out;
    const QFileInfoList subdirs =
        base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &subdirInfo : subdirs) {
        const QDir d(subdirInfo.absoluteFilePath());
        const QStringList gguFs =
            ModelCatalog::allGguf(d.entryList(QDir::Files, QDir::Name));
        if (gguFs.isEmpty())
            continue;

        // A repo directory may hold several quants of the same model (e.g.
        // Unlimited-OCR-Q4_K_M.gguf + Unlimited-OCR-Q8_0.gguf) sharing one
        // mmproj. Emit one entry per quant; split parts of one quant are
        // grouped into a single entry.
        QString mmprojRel;
        QHash<QString, QStringList> byStem;
        for (const QString &name : gguFs) {
            if (ModelCatalog::fileKind(name) == ModelFileKind::Vision) {
                if (mmprojRel.isEmpty())
                    mmprojRel = name;
            } else {
                byStem[modelStem(name)].append(name);
            }
        }
        if (byStem.isEmpty())
            continue;

        const QString dirName = subdirInfo.fileName();
        const QString dirPath = subdirInfo.canonicalFilePath();
        const QString mmprojPath =
            mmprojRel.isEmpty() ? QString() : d.filePath(mmprojRel);

        QStringList stems = byStem.keys();
        stems.sort();
        for (const QString &stem : stems) {
            QStringList modelParts = byStem.value(stem);
            // Split names sort so the "…-00001-of-NNN" part is first.
            sortSplitParts(&modelParts);

            ModelEntry e;
            const QString quant =
                ModelCatalog::quantizationFromName(modelParts.first());
            e.id = quant.isEmpty() ? dirName
                                   : dirName + QLatin1Char('_') + quant;
            e.title = dirName;
            // The dir name "org__repo" is not a valid HF repo id; reconstruct
            // org/repo from it (§3.12). A persisted repoId (set at install time)
            // wins when the entry is later loaded from a real index.json.
            const int sep = dirName.indexOf(QLatin1String("__"));
            e.repoId = sep > 0
                ? dirName.left(sep) + QLatin1Char('/') + dirName.mid(sep + 2)
                : dirName;
            e.repo = e.repoId;
            e.dir = dirPath;
            e.origin = ModelOrigin::Managed;  // inside modelsDir ⇒ managed
            e.license = QStringLiteral("https://huggingface.co/%1").arg(e.repo);
            e.addedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            e.modelPath = d.filePath(modelParts.first());
            if (modelParts.size() > 1)
                e.parts = {modelParts.cbegin() + 1, modelParts.cend()};
            e.mmprojPath = mmprojPath;
            e.quantization = quant;
            qint64 total = 0;
            for (const QString &name : modelParts)
                total += QFileInfo(d.filePath(name)).size();
            e.byteSize = total;
            out.append(std::move(e));
        }
    }
    return out;
}

QString ModelRegistry::removalError(const ModelEntry &e, const QString &modelsDir,
                                    bool active, bool runtimeReady)
{
    if (e.origin != ModelOrigin::Managed)
        return QObject::tr(
            "This model is external and can only be hidden from the list, not deleted");
    // Canonical when it exists (symlink-resolved, §5 task 6), otherwise absolute:
    // the removal guard must also work for paths that only exist conceptually (
    // tests) and for directories about to be created.
    const QString modelPath =
        canonicalPath(e.modelPath).isEmpty()
            ? QFileInfo(e.modelPath).absoluteFilePath()
            : canonicalPath(e.modelPath);
    const QString dirPath = canonicalPath(modelsDir).isEmpty()
                                ? QFileInfo(modelsDir).absoluteFilePath()
                                : canonicalPath(modelsDir);
    if (dirPath.isEmpty() || modelPath.isEmpty()
        || !isSubpathOf(modelPath, dirPath))
        return QObject::tr(
            "This model file lies outside the models directory and cannot be removed.");
    if (active && runtimeReady)
        return QObject::tr("This model is in use. Stop the server before removing it.");
    return QString();
}

QString ModelRegistry::canonicalPath(const QString &p)
{
    return QFileInfo(p).canonicalFilePath();
}

}  // namespace llocr