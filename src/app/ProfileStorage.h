#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

class QJsonObject;

namespace llocr {

namespace ProfileStorage {

bool removeFileIfExists(const QString &path, QString *error = nullptr);
bool writeJsonAtomic(const QString &path, const QJsonObject &root,
                     QString *error = nullptr);

QJsonDocument readJson(const QString &path, bool *ok, QString *error = nullptr);

/// The envelope every profile file shares: `{"schemaVersion": N, …body}`.
///
/// `readEnvelope` is the one place that checks the version. The launch and
/// request profile files carried a `schemaVersion` that nothing ever read, so a
/// file written by a newer build was accepted as if it were current. A file
/// from the future is now reported through `*error` — the caller still applies
/// what it can parse, but no longer silently.
///
/// `storeName` only prefixes the diagnostic, so each store keeps its own
/// translation context and its own wording.
QJsonDocument readEnvelope(const QString &path, int expectedSchemaVersion,
                           const QString &storeName, bool *ok, QString *error = nullptr);

/// Writes `schemaVersion` plus `body`; the version is always stamped from the
/// current build, never copied from what was read.
bool writeEnvelope(const QString &path, int schemaVersion, const QJsonObject &body,
                   QString *error = nullptr);

}  // namespace ProfileStorage

}  // namespace llocr
