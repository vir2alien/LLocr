#pragma once

#include <QString>

#include "core/StatusMessage.h"

namespace llocr {

enum class CheckStatus : int {
    Failed = 0,        ///< Network/parse error — errorMessage describes it.
    Ok = 1,            ///< The decision model confirmed the match (or the verifier's old OK).
    ReRecognized = 2,  ///< The block-recognition reply is the block text — text holds it.
    Review = 3,        ///< The block is unreadable — needs human eyes.
    Mismatch = 4,      ///< The decision model rejected the match (below the threshold).
};

struct CheckResult {
    CheckStatus status = CheckStatus::Failed;
    QString text;  ///< Recognized block text when status == ReRecognized.
    StatusMessage errorMessage;
    double confidence = -1.0;  ///< P(match) of the decision verdict; -1 when the verdict carries none.

    static CheckResult makeError(const StatusMessage &message)
    {
        CheckResult result;
        result.status = CheckStatus::Failed;
        result.errorMessage = message;
        return result;
    }
};

}  // namespace llocr