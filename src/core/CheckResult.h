#pragma once

#include <QString>

#include "core/StatusMessage.h"

namespace llocr {

enum class CheckStatus : int {
    Failed = 0,    ///< Network/parse error — errorMessage describes it.
    Ok = 1,        ///< The decision model confirmed the match (or the verifier's old OK).
    Fixed = 2,     ///< The re-recognition returned the block text — text holds it.
    Review = 3,    ///< The block is unreadable — needs human eyes.
    Mismatch = 4,  ///< The decision model rejected the match (below the threshold).
};

struct CheckResult {
    CheckStatus status = CheckStatus::Failed;
    QString text;  ///< Corrected block text when status == Fixed.
    StatusMessage errorMessage;

    static CheckResult makeError(const StatusMessage &message)
    {
        CheckResult result;
        result.status = CheckStatus::Failed;
        result.errorMessage = message;
        return result;
    }
};

}  // namespace llocr