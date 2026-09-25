#pragma once

#include <QJsonDocument>
#include <QString>

class QJsonObject;

namespace llocr {

namespace ProfileStorage {

bool removeFileIfExists(const QString &path, QString *error = nullptr);
bool writeJsonAtomic(const QString &path, const QJsonObject &root,
                     QString *error = nullptr);

QJsonDocument readJson(const QString &path, bool *ok, QString *error = nullptr);

}  // namespace ProfileStorage

}  // namespace llocr
