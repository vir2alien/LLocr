#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "runtime/ModelCatalog.h"
#include "runtime/ModelPresetCatalog.h"

namespace llocr {

namespace {

constexpr const char *kUserSchemaKey = "schemaVersion";
constexpr int kUserSchemaVersion = 1;
constexpr const char *kModelsKey = "models";

QList<ModelPreset> readFile(const QString &path, const QString &fileDesc, QString &error, bool missingIsOk)
{
    QFile f(path);
    if (!f.exists()) {
        if (!missingIsOk)
            error = QObject::tr("%1 not found: %2").arg(fileDesc, path);
        return QList<ModelPreset>();
    }
    if (!f.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Unable to read %1: %2").arg(fileDesc, path);
        return QList<ModelPreset>();
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        error = QObject::tr("%1 is malformed: %2").arg(fileDesc, perr.errorString());
        return QList<ModelPreset>();
    }
    QJsonArray arr;
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        if (root.contains(QLatin1String(kUserSchemaKey))) {
            const int version = root.value(QLatin1String(kUserSchemaKey)).toInt(-1);
            if (version > kUserSchemaVersion) {
                error = QObject::tr("%1 uses an unsupported schema version (%2; supported: %3)").arg(fileDesc).arg(version).arg(kUserSchemaVersion);
                return QList<ModelPreset>();
            }
        }
        arr = root.value(QLatin1String(kModelsKey)).toArray();
    } else if (doc.isArray()) {
        arr = doc.array();
    } else {
        error = QObject::tr("%1 has an unexpected shape").arg(fileDesc);
        return QList<ModelPreset>();
    }
    return ModelPresetCatalog::parse(arr, error);
}

}  // namespace

QList<ModelPreset> ModelPresetCatalog::parse(const QJsonArray &arr, QString &error)
{
    QList<ModelPreset> out;
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const ModelPreset p = ModelPreset::fromJson(v.toObject());
        if (p.id.isEmpty() || (p.repo.isEmpty() && p.model.isEmpty()))
            continue;
        out.append(p);
    }
    if (out.isEmpty() && !arr.isEmpty())
        error = QObject::tr("The preset catalog contained no usable presets");
    return out;
}

QJsonArray ModelPresetCatalog::toArray(const QList<ModelPreset> &presets)
{
    QJsonArray arr;
    for (const ModelPreset &p : presets)
        arr.append(p.toJson());
    return arr;
}

QString ModelPresetCatalog::entryId(const QString &profileId, const QString &quantId)
{
    return quantId.isEmpty() ? profileId : profileId + QLatin1Char('-') + quantId;
}

QList<ModelPreset> ModelPresetCatalog::expand(const QList<ModelProfiles::Profile> &profiles)
{
    QList<ModelPreset> out;
    for (const ModelProfiles::Profile &profile : profiles) {
        for (const ModelProfiles::Quant &quant : profile.files.quants) {
            ModelPreset preset;
            preset.id = entryId(profile.id, quant.id);
            preset.profileId = profile.id;
            preset.title = QStringLiteral("%1 (%2)").arg(profile.title, quant.id);
            preset.repo = profile.files.repo;
            preset.revision = profile.files.revision;
            preset.model = quant.file;
            preset.mmproj = profile.files.mmproj.file;
            if (!profile.files.mtp.file.isEmpty()) {
                preset.mtp = profile.files.mtp.file;
                preset.mtpRepo = profile.files.mtp.repo.isEmpty() ? profile.files.repo : profile.files.mtp.repo;
                preset.mtpRevision = profile.files.mtp.revision;
            }
            preset.minBuild = profile.minBuild;
            preset.license = profile.license;
            preset.ctxSize = 8192;
            if (!quant.sha256.isEmpty())
                preset.sha256.insert(ModelCatalog::leafName(quant.file).toLower(), quant.sha256);
            if (!profile.files.mmproj.sha256.isEmpty())
                preset.sha256.insert(ModelCatalog::leafName(profile.files.mmproj.file).toLower(), profile.files.mmproj.sha256);
            if (!profile.files.mtp.sha256.isEmpty())
                preset.sha256.insert(ModelCatalog::leafName(profile.files.mtp.file).toLower(), profile.files.mtp.sha256);
            out.append(preset);
        }
    }
    return out;
}

QList<ModelPreset> ModelPresetCatalog::load(const QList<ModelPreset> &builtIn, const QString &userCatalogPath, QString &error)
{
    QString userErr;
    const QList<ModelPreset> user = readFile(userCatalogPath, QObject::tr("user preset catalog"), userErr, /*missingIsOk=*/true);

    QList<ModelPreset> merged;
    QHash<QString, int> indexById;
    for (const ModelPreset &p : builtIn) {
        indexById.insert(p.id, merged.size());
        merged.append(p);
    }
    for (const ModelPreset &p : user) {
        const auto it = indexById.constFind(p.id);
        if (it != indexById.constEnd()) {
            merged[*it] = p;
        } else {
            indexById.insert(p.id, merged.size());
            merged.append(p);
        }
    }
    return merged;
}

bool ModelPresetCatalog::save(const QString &userCatalogPath, const QList<ModelPreset> &presets, QString &error)
{
    QDir().mkpath(QFileInfo(userCatalogPath).absolutePath());

    QJsonObject root;
    root.insert(QLatin1String(kUserSchemaKey), kUserSchemaVersion);
    root.insert(QLatin1String(kModelsKey), toArray(presets));

    QSaveFile f(userCatalogPath);
    if (!f.open(QIODevice::WriteOnly)) {
        error = QObject::tr("Unable to write preset catalog: %1").arg(f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        error = QObject::tr("Unable to commit preset catalog: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool ModelPresetCatalog::resetUserCatalog(const QString &userCatalogPath, QString &error)
{
    QFile f(userCatalogPath);
    if (f.exists()) {
        if (!f.remove()) {
            error = QObject::tr("Unable to remove user catalog: %1").arg(f.errorString());
            return false;
        }
    }
    return true;
}

}  // namespace llocr