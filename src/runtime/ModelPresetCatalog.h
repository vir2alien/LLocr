#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "runtime/ModelPreset.h"

namespace llocr {

class ModelPresetCatalog
{
public:
    static constexpr const char *kBuiltInPath = ":/models/default-presets.json";

    static QList<ModelPreset> load(const QString &userCatalogPath, QString &error);
    static QList<ModelPreset> parse(const QJsonArray &arr, QString &error);
    static QJsonArray toArray(const QList<ModelPreset> &presets);
    static bool save(const QString &userCatalogPath, const QList<ModelPreset> &presets,
                     QString &error);
    static bool resetUserCatalog(const QString &userCatalogPath, QString &error);
};

}  // namespace llocr