#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "runtime/ModelPreset.h"

namespace llocr {

// Model preset catalogs (§ Stage E task 4, ADR 42). Two sources are merged by
// `id`: a built-in catalog shipped read-only inside the Qt resources
// (:/models/default-presets.json) and a user-editable catalog at
// <AppData>/LLocr/models/catalog.json. The user catalog overrides the built-in
// one when they share an id.
class ModelPresetCatalog
{
public:
    static constexpr const char *kBuiltInPath = ":/models/default-presets.json";

    // Reads the built-in catalog + (optional) user catalog and merges them by
    // id (user wins). On a missing/corrupt built-in catalog an empty list is
    // returned with `error` set; a missing user catalog is not an error.
    static QList<ModelPreset> load(const QString &userCatalogPath, QString &error);

    /// Parses an array of preset JSON objects into ModelPreset records.
    static QList<ModelPreset> parse(const QJsonArray &arr, QString &error);

    /// Serializes presets to a built-in-style JSON array.
    static QJsonArray toArray(const QList<ModelPreset> &presets);

    /// Writes `presets` to `userCatalogPath` via QSaveFile (schemaVersion kept).
    /// Returns true on success, false + `error` otherwise.
    static bool save(const QString &userCatalogPath, const QList<ModelPreset> &presets,
                     QString &error);

    /// Removes the user catalog file (used by "Restore defaults").
    static bool resetUserCatalog(const QString &userCatalogPath, QString &error);
};

}  // namespace llocr