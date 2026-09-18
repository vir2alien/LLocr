#include "models/QwenGeneralModel.h"

namespace llocr {

QString QwenGeneralModel::id() const
{
    return QStringLiteral("qwen-general");
}

QString QwenGeneralModel::displayName() const
{
    return QStringLiteral("Qwen (general purpose)");
}

} // namespace llocr