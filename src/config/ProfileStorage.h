#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

class QJsonObject;

namespace llocr {

namespace ProfileStorage {

bool removeFileIfExists(const QString &path, QString *error = nullptr);
bool writeJsonAtomic(const QString &path, const QJsonObject &root, QString *error = nullptr);

QJsonDocument readJson(const QString &path, bool *ok, QString *error = nullptr);

QJsonDocument readEnvelope(const QString &path, int expectedSchemaVersion, const QString &storeName, bool *ok, QString *error = nullptr);

bool writeEnvelope(const QString &path, int schemaVersion, const QJsonObject &body, QString *error = nullptr);

}  // namespace ProfileStorage

}  // namespace llocr
