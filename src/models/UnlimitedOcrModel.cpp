#include "models/UnlimitedOcrModel.h"

namespace llocr {

QString UnlimitedOcrModel::id() const
{
    return QStringLiteral("unlimited-ocr");
}

QString UnlimitedOcrModel::displayName() const
{
    return QStringLiteral("Unlimited-OCR");
}

QList<OcrPromptVariant> UnlimitedOcrModel::promptVariants() const
{
    return { OcrPromptVariant{ QStringLiteral("document-parsing"),
                               QStringLiteral("Document parsing"),
                               QStringLiteral("document parsing.") } };
}

QString UnlimitedOcrModel::defaultParserId() const
{
    return QStringLiteral("det_tokens");
}

} // namespace llocr
