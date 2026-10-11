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

// Reads and validates the envelope. *ok is false for an unreadable file, a
// non-object document, a missing schemaVersion — and for a version newer than
// expectedSchemaVersion; only the last case also sets *tooNew, telling the
// caller the on-disk file must not be rewritten in the old format.
QJsonDocument readEnvelope(const QString &path, int expectedSchemaVersion, const QString &storeName, bool *ok, QString *error = nullptr, bool *tooNew = nullptr);

bool writeEnvelope(const QString &path, int schemaVersion, const QJsonObject &body, QString *error = nullptr);

}  // namespace ProfileStorage

}  // namespace llocr
