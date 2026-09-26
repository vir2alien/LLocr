#include "models/OcrModelFactory.h"

#include <QDebug>

#include "models/Lfm25VlModel.h"
#include "models/UnlimitedOcrModel.h"

namespace llocr {

static const QStringList kModelIds = {
    QStringLiteral("unlimited-ocr"),
    QStringLiteral("lfm25-vl-3b"),
};

std::unique_ptr<OcrModel> OcrModelFactory::create(const QString &modelId)
{
    if (modelId == QStringLiteral("unlimited-ocr"))
        return std::make_unique<UnlimitedOcrModel>();
    if (modelId == QStringLiteral("lfm25-vl-3b"))
        return std::make_unique<Lfm25VlModel>();
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
    // No silent substitution: an unknown display name yields an empty id, so a
    // caller cannot end up "selecting" the default model by accident (ADR 110).
    // The UI works on ids (OcrModelListModel) and no longer needs this at all.
    return QString();
}

} // namespace llocr
