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
    return ModelProfiles::defaultIdForRole(ModelProfiles::instance(), QStringLiteral("ocr"));
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
