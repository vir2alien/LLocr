#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>

#include "core/ModelProfiles.h"
#include "runtime/ModelPreset.h"

namespace llocr {

class ModelPresetCatalog
{
public:
    static QList<ModelPreset> expand(const QList<ModelProfiles::Profile> &profiles);

    static QList<ModelPreset> load(const QList<ModelPreset> &builtIn, const QString &userCatalogPath, QString &error);
    static QList<ModelPreset> parse(const QJsonArray &arr, QString &error);
    static QJsonArray toArray(const QList<ModelPreset> &presets);
    static bool save(const QString &userCatalogPath, const QList<ModelPreset> &presets, QString &error);
    static bool resetUserCatalog(const QString &userCatalogPath, QString &error);

    static QString entryId(const QString &profileId, const QString &quantId);
};

}  // namespace llocr
