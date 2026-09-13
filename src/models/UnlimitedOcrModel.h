#pragma once

#include "models/OcrModel.h"

namespace llocr {

class UnlimitedOcrModel : public OcrModel {
    Q_DISABLE_COPY_MOVE(UnlimitedOcrModel)

public:
    UnlimitedOcrModel() = default;

    QString id() const override;
    QString displayName() const override;
    QList<OcrPromptVariant> promptVariants() const override;
    QString defaultParserId() const override;
};

} // namespace llocr
