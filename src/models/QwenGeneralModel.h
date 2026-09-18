#pragma once

#include "models/GeneralPurposeModel.h"

namespace llocr {

// General-purpose model family currently used for the text-verification flow.
// The actual model served (e.g. qwen3.5-4b) is chosen by the connection; this
// class only describes the family so a factory/settings UI can be added later.
class QwenGeneralModel : public GeneralPurposeModel {
    Q_DISABLE_COPY_MOVE(QwenGeneralModel)

public:
    QwenGeneralModel() = default;

    QString id() const override;
    QString displayName() const override;
};

} // namespace llocr