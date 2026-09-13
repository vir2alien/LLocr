#include "models/OcrModelFactory.h"

#include <QDebug>

#include "models/UnlimitedOcrModel.h"

namespace llocr {

static const QStringList kModelIds = {
    QStringLiteral("unlimited-ocr"),
};

std::unique_ptr<OcrModel> OcrModelFactory::create(const QString &modelId)
{
    if (modelId == QStringLiteral("unlimited-ocr"))
        return std::make_unique<UnlimitedOcrModel>();
    qWarning() << "OcrModelFactory: unknown model id" << modelId
               << "— falling back to the default model";
    return std::make_unique<UnlimitedOcrModel>();
}

QStringList OcrModelFactory::registeredIds()
{
    return kModelIds;
}

QString OcrModelFactory::defaultId()
{
    return kModelIds.first();
}

QString OcrModelFactory::displayNameForId(const QString &modelId)
{
    const std::unique_ptr<OcrModel> model = create(modelId);
    return model->displayName();
}

QString OcrModelFactory::idForDisplayName(const QString &displayName)
{
    for (const QString &id : kModelIds) {
        const std::unique_ptr<OcrModel> model = create(id);
        if (model->displayName() == displayName)
            return id;
    }
    return defaultId();
}

} // namespace llocr
