#pragma once

#include <QJsonDocument>
#include <QString>

class QJsonObject;

namespace llocr {

// Shared JSON persistence helpers for the profile stores (launch profiles,
// request profiles, verification prompts). Each store keeps its own tr()
// warning texts; these helpers only return a diagnostic error string.
namespace ProfileStorage {

// Removes the file if it exists. Returns true when the file is gone
// (or never existed); false when removal failed and *error was set.
bool removeFileIfExists(const QString &path, QString *error = nullptr);

// Atomically writes the JSON object: mkpath on the parent dir, then
// QSaveFile + commit. Returns false and sets *error on failure.
bool writeJsonAtomic(const QString &path, const QJsonObject &root,
                     QString *error = nullptr);

// Reads and parses a JSON file. On success returns the document and sets
// *ok = true. On a missing file, an unreadable file or a parse error,
// returns a null document, sets *ok = false and fills *error with a
// diagnostic (missing file is reported through ok, not error).
QJsonDocument readJson(const QString &path, bool *ok, QString *error = nullptr);

}  // namespace ProfileStorage

}  // namespace llocr
