#include "config/ProfileStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

namespace llocr {

namespace ProfileStorage {

namespace {
constexpr const char kSchemaVersionKey[] = "schemaVersion";
}  // namespace

bool removeFileIfExists(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.remove()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool writeJsonAtomic(const QString &path, const QJsonObject &root, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QJsonDocument readJson(const QString &path, bool *ok, QString *error)
{
    *ok = false;
    QFile file(path);
    if (!file.exists())
        return QJsonDocument();
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return QJsonDocument();
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return QJsonDocument();
    }
    *ok = true;
    return doc;
}

QJsonDocument readEnvelope(const QString &path, int expectedSchemaVersion, const QString &storeName, bool *ok, QString *error)
{
    QJsonDocument doc = readJson(path, ok, error);
    if (!*ok || !doc.isObject())
        return doc;

    const int version = doc.object().value(QLatin1String(kSchemaVersionKey)).toInt(-1);
    if (version < 0) {
        if (error)
            *error = QStringLiteral("no schemaVersion");
    } else if (version > expectedSchemaVersion) {
        if (error) {
            *error = QStringLiteral("%1: schema version %2 is newer than the supported %3").arg(storeName).arg(version).arg(expectedSchemaVersion);
        }
    }
    return doc;
}

bool writeEnvelope(const QString &path, int schemaVersion, const QJsonObject &body, QString *error)
{
    QJsonObject root = body;
    root.insert(QLatin1String(kSchemaVersionKey), schemaVersion);
    return writeJsonAtomic(path, root, error);
}

}  // namespace ProfileStorage

}  // namespace llocr
