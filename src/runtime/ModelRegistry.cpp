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

#include "runtime/InstalledReconcile.h"
#include "runtime/ModelCatalog.h"
#include "runtime/ModelRegistry.h"

namespace llocr {

namespace {

constexpr const char *kIndexFile = "index.json";
constexpr const char *kLockFile = ".registry.lock";
constexpr const char *kModelsKey = "models";

QString originToString(ModelOrigin o)
{
    return o == ModelOrigin::Managed ? QStringLiteral("managed") : QStringLiteral("external");
}

ModelOrigin originFromString(const QString &s)
{
    return s == QLatin1String("managed") ? ModelOrigin::Managed : ModelOrigin::External;
}

QString roleFromIndex(const QString &s)
{
    // Index files written before the role rename carry "check".
    return s == QLatin1String("check") ? QStringLiteral("blockRecognition") : s;
}

bool isSubpathOf(const QString &path, const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString d = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    if (d.isEmpty())
        return false;
    return p == d || p.startsWith(d + QLatin1Char('/'));
}

void sortSplitParts(QStringList *names)
{
    std::sort(names->begin(), names->end(), &ModelCatalog::splitAscending);
}

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
    e.draftPath = o.value(QStringLiteral("draftPath")).toString();
    e.dir = o.value(QStringLiteral("dir")).toString();
    e.origin = originFromString(o.value(QStringLiteral("managed")).toString());
    e.byteSize = static_cast<qint64>(o.value(QStringLiteral("byteSize")).toDouble(0));
    e.quantization = o.value(QStringLiteral("quantization")).toString();
    e.license = o.value(QStringLiteral("license")).toString();
    e.sha256 = o.value(QStringLiteral("sha256")).toString();
    e.parser = o.value(QStringLiteral("parser")).toString();
    if (o.contains(QStringLiteral("ctxSize"))) {
        e.ctxSize = o.value(QStringLiteral("ctxSize")).toInt();
        e.ctxSizeSet = true;
    }
    e.addedAt = o.value(QStringLiteral("addedAt")).toString();
    e.repoId = o.value(QStringLiteral("repoId")).toString();
    const QJsonArray roles = o.value(QStringLiteral("roles")).toArray();
    for (const QJsonValue &v : roles)
        e.roles << roleFromIndex(v.toString());
    const QJsonArray parts = o.value(QStringLiteral("parts")).toArray();
    for (const QJsonValue &v : parts)
        e.parts << v.toString();
    if (e.dir.isEmpty() && !e.modelPath.isEmpty())
        e.dir = QFileInfo(e.modelPath).absolutePath();
    return e;
}

QList<ModelEntry> entriesFromIndex(const QJsonObject &root)
{
    QList<ModelEntry> out;
    const QJsonArray arr = root.value(QLatin1String(kModelsKey)).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        ModelEntry e = entryFromJson(v.toObject());
        if (!e.modelPath.isEmpty() || !e.dir.isEmpty())
            out.append(std::move(e));
    }
    return out;
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
    if (!e.draftPath.isEmpty())
        o.insert(QStringLiteral("draftPath"), e.draftPath);
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
    if (!e.roles.isEmpty()) {
        QJsonArray arr;
        for (const QString &r : e.roles)
            arr.append(r);
        o.insert(QStringLiteral("roles"), arr);
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

QList<ModelEntry> ModelRegistry::load(const QString &modelsDir, bool &rebuilt, QString &error)
{
    return load(modelsDir, rebuilt, error, nullptr, ReconcileSelections());
}

QList<ModelEntry> ModelRegistry::load(const QString &modelsDir, bool &rebuilt, QString &error, ReconcileResult *report, const ReconcileSelections &selections)
{
    rebuilt = false;
    // One path for every way the index can be unusable: a missing, unreadable,
    // corrupt or version-mismatched file is simply an empty index, and the
    // reconciliation fills it from the scan. The four near-identical rescan
    // branches this replaces each had to remember to do the same thing.
    QList<ModelEntry> index;
    QFile f(indexPathFor(modelsDir));
    const bool indexExists = f.exists();
    if (indexExists) {
        if (!f.open(QIODevice::ReadOnly)) {
            error = QObject::tr("Unable to read model registry: %1").arg(f.errorString());
        } else {
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                error = QObject::tr("Model index is corrupt; rescanning models directory");
            } else if (doc.object().value(QStringLiteral("schemaVersion")).toInt(-1) != kSchemaVersion) {
                error = QObject::tr("Model index version mismatch; rescanning");
            } else {
                index = entriesFromIndex(doc.object());
            }
        }
    }
    rebuilt = !indexExists || !error.isEmpty();

    ReconcileInput input;
    input.index = index;
    // A models directory that is not there is *unknown*, not empty: a removable
    // drive that is not mounted must not be reported as "every model deleted".
    input.diskAvailable = QFileInfo(modelsDir).isDir();
    input.disk = input.diskAvailable ? scanModelsDir(modelsDir) : QList<ModelEntry>();
    input.selectedModelPath = selections.modelPath;
    input.selectedCheckModelPath = selections.checkModelPath;
    input.selectedDecisionModelPath = selections.decisionModelPath;
    input.selectedLayoutModelPath = selections.layoutModelPath;
    input.selectedServerPath = selections.serverPath;
    input.selectedServerExists = selections.serverExists;

    const ReconcileResult result = reconcileInstalled(input);
    if (report)
        *report = result;
    if (result.indexChanged && input.diskAvailable) {
        QString saveErr;
        if (!save(modelsDir, result.models, saveErr) && error.isEmpty())
            error = saveErr;
    }
    return result.models;
}

bool ModelRegistry::save(const QString &modelsDir, const QList<ModelEntry> &entries, QString &error)
{
    QDir().mkpath(modelsDir);

    QLockFile lock(lockPathFor(modelsDir));
    lock.setStaleLockTime(30 * 1000);
    if (!lock.tryLock(5000)) {
        error = QObject::tr("Model registry is locked by another LLocr instance");
        return false;
    }
    return writeIndex(modelsDir, entries, error);
}

bool ModelRegistry::update(const QString &modelsDir, const std::function<QList<ModelEntry>(QList<ModelEntry> &)> &mutate, QString &error)
{
    QDir().mkpath(modelsDir);

    QLockFile lock(lockPathFor(modelsDir));
    lock.setStaleLockTime(30 * 1000);
    if (!lock.tryLock(5000)) {
        error = QObject::tr("Model registry is locked by another LLocr instance");
        return false;
    }

    // Read under the same lock that guards the write, so a second instance
    // installing a model cannot have its entry dropped by a first-writer-wins
    // save built from an unlocked snapshot.
    QList<ModelEntry> entries = readIndex(modelsDir, error);
    error.clear();
    return writeIndex(modelsDir, mutate(entries), error);
}

QList<ModelEntry> ModelRegistry::readIndex(const QString &modelsDir, QString &error)
{
    QList<ModelEntry> out;
    QFile f(indexPathFor(modelsDir));
    if (!f.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Unable to read model registry: %1").arg(f.errorString());
        return out;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QObject::tr("Model index is corrupt");
        return out;
    }
    return entriesFromIndex(doc.object());
}

bool ModelRegistry::writeIndex(const QString &modelsDir, const QList<ModelEntry> &entries, QString &error)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    QJsonArray arr;
    for (const ModelEntry &e : entries)
        arr.append(entryToJson(e));
    root.insert(QLatin1String(kModelsKey), arr);

    QSaveFile f(indexPathFor(modelsDir));
    f.setDirectWriteFallback(true);
    if (!f.open(QIODevice::WriteOnly)) {
        error = QObject::tr("Unable to open model index for writing: %1").arg(f.errorString());
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
    const QFileInfoList subdirs = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &subdirInfo : subdirs) {
        const QDir d(subdirInfo.absoluteFilePath());
        const QStringList gguFs = ModelCatalog::allGguf(d.entryList(QDir::Files, QDir::Name));
        if (gguFs.isEmpty())
            continue;

        QString mmprojRel;
        QString draftRel;
        QHash<QString, QStringList> byStem;
        for (const QString &name : gguFs) {
            const ModelFileKind kind = ModelCatalog::fileKind(name);
            if (kind == ModelFileKind::Vision) {
                if (mmprojRel.isEmpty())
                    mmprojRel = name;
            } else if (kind == ModelFileKind::Draft) {
                if (draftRel.isEmpty())
                    draftRel = name;
            } else {
                byStem[modelStem(name)].append(name);
            }
        }
        if (byStem.isEmpty())
            continue;

        const QString dirName = subdirInfo.fileName();
        const QString dirPath = subdirInfo.canonicalFilePath();
        const QString mmprojPath = mmprojRel.isEmpty() ? QString() : d.filePath(mmprojRel);
        const QString draftPath = draftRel.isEmpty() ? QString() : d.filePath(draftRel);

        QStringList stems = byStem.keys();
        stems.sort();
        for (const QString &stem : stems) {
            QStringList modelParts = byStem.value(stem);
            sortSplitParts(&modelParts);

            ModelEntry e;
            const QString quant = ModelCatalog::quantizationFromName(modelParts.first());
            e.id = quant.isEmpty() ? dirName : dirName + QLatin1Char('_') + quant;
            e.title = dirName;
            const int sep = dirName.indexOf(QLatin1String("__"));
            e.repoId = sep > 0 ? dirName.left(sep) + QLatin1Char('/') + dirName.mid(sep + 2) : dirName;
            e.repo = e.repoId;
            e.dir = dirPath;
            e.origin = ModelOrigin::Managed;  // inside modelsDir ⇒ managed
            e.license = QStringLiteral("https://huggingface.co/%1").arg(e.repo);
            e.addedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            e.modelPath = d.filePath(modelParts.first());
            if (modelParts.size() > 1)
                e.parts = {modelParts.cbegin() + 1, modelParts.cend()};
            e.mmprojPath = mmprojPath;
            e.draftPath = draftPath;
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

QString ModelRegistry::removalError(const ModelEntry &e, const QString &modelsDir, bool active, bool runtimeReady)
{
    if (e.origin != ModelOrigin::Managed)
        return QObject::tr("This model is external and can only be hidden from the list, not deleted");
    const QString modelPath = canonicalPath(e.modelPath).isEmpty() ? QFileInfo(e.modelPath).absoluteFilePath() : canonicalPath(e.modelPath);
    const QString dirPath = canonicalPath(modelsDir).isEmpty() ? QFileInfo(modelsDir).absoluteFilePath() : canonicalPath(modelsDir);
    if (dirPath.isEmpty() || modelPath.isEmpty() || !isSubpathOf(modelPath, dirPath))
        return QObject::tr("This model file lies outside the models directory and cannot be removed.");
    if (active && runtimeReady)
        return QObject::tr("This model is in use. Stop the server before removing it.");
    return QString();
}

QString ModelRegistry::canonicalPath(const QString &p)
{
    return QFileInfo(p).canonicalFilePath();
}

}  // namespace llocr