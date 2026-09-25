#pragma once

#include "models/GeneralPurposeModel.h"

namespace llocr {

class QwenGeneralModel : public GeneralPurposeModel {
    Q_DISABLE_COPY_MOVE(QwenGeneralModel)

public:
    QwenGeneralModel() = default;

    QString id() const override;
    QString displayName() const override;
};

} // namespace llocr
