#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "runtime/ModelPresetCatalog.h"

namespace llocr {

namespace {

constexpr const char *kUserSchemaKey = "schemaVersion";
constexpr int kUserSchemaVersion = 1;
constexpr const char *kModelsKey = "models";

QList<ModelPreset> readFile(const QString &path, const QString &fileDesc,
                            QString &error, bool missingIsOk)
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
        arr = doc.object().value(QLatin1String(kModelsKey)).toArray();
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

QList<ModelPreset> ModelPresetCatalog::load(const QString &userCatalogPath,
                                            QString &error)
{
    QList<ModelPreset> out;

    // Built-in catalog is resource-backed; failures here are real errors.
    QString builtinErr;
    QList<ModelPreset> builtIn = readFile(QLatin1String(kBuiltInPath),
                                           QObject::tr("built-in preset catalog"),
                                           builtinErr, /*missingIsOk=*/false);
    if (builtIn.isEmpty() && !builtinErr.isEmpty()) {
        error = builtinErr;
        return QList<ModelPreset>();
    }

    // User catalog overrides by id; a missing file is not an error.
    QString userErr;
    QList<ModelPreset> user =
        readFile(userCatalogPath, QObject::tr("user preset catalog"), userErr,
                 /*missingIsOk=*/true);

    // Merge by id: keep built-in first, then replace/append user entries.
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

bool ModelPresetCatalog::save(const QString &userCatalogPath,
                              const QList<ModelPreset> &presets, QString &error)
{
    QDir().mkpath(QFileInfo(userCatalogPath).absolutePath());

    QJsonObject root;
    root.insert(QLatin1String(kUserSchemaKey), kUserSchemaVersion);
    root.insert(QLatin1String(kModelsKey), toArray(presets));

    QSaveFile f(userCatalogPath);
    if (!f.open(QIODevice::WriteOnly)) {
        error = QObject::tr("Unable to write preset catalog: %1")
                    .arg(f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        error = QObject::tr("Unable to commit preset catalog: %1")
                    .arg(f.errorString());
        return false;
    }
    return true;
}

bool ModelPresetCatalog::resetUserCatalog(const QString &userCatalogPath,
                                          QString &error)
{
    QFile f(userCatalogPath);
    if (f.exists()) {
        if (!f.remove()) {
            error = QObject::tr("Unable to remove user catalog: %1")
                        .arg(f.errorString());
            return false;
        }
    }
    return true;
}

}  // namespace llocr