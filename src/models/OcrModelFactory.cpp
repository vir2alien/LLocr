#include "models/OcrModelFactory.h"

#include <QDebug>

#include "core/ModelProfiles.h"

namespace llocr {

std::unique_ptr<OcrModel> OcrModelFactory::create(const QString &modelId)
{
    if (ModelProfiles::find(ModelProfiles::instance(), modelId))
        return std::make_unique<OcrModel>(modelId);

    const QString fallback = defaultId();
    qWarning() << "OcrModelFactory: unknown model id" << modelId << "— falling back to" << fallback;
    return std::make_unique<OcrModel>(fallback);
}

QStringList OcrModelFactory::registeredIds()
{
    return ModelProfiles::idsForRole(ModelProfiles::instance(), QStringLiteral("ocr"));
}

QString OcrModelFactory::defaultId()
{
    const QList<ModelProfiles::Profile> &profiles = ModelProfiles::instance();
    for (const ModelProfiles::Profile &profile : profiles) {
        if (profile.isDefault && ModelProfiles::roleFor(profile, QStringLiteral("ocr")))
            return profile.id;
    }
    // A profile that opts in is the only reliable default: the catalog is
    // loaded in file-name order, so "first" would change with the file names.
    const QStringList ids = registeredIds();
    return ids.isEmpty() ? QString() : ids.first();
}

QString OcrModelFactory::displayNameForId(const QString &modelId)
{
    return create(modelId)->displayName();
}

QString OcrModelFactory::idForDisplayName(const QString &displayName)
{
    for (const QString &id : registeredIds()) {
        if (create(id)->displayName() == displayName)
            return id;
    }
    return QString();
}

}  // namespace llocr
