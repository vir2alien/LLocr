#pragma once

#include <QString>

#include "core/StatusMessage.h"

namespace llocr {

// Transport status of one model reply; not persisted. The producers:
// blockRecognition → Recognized (the reply is the block text) or Review
// (unreadable), fix → Ok / Fixed (text holds the correction) / Review,
// decision → Ok / Mismatch (confidence carries the probability).
enum class CheckStatus : int {
    Failed = 0,      ///< Network/parse error — errorMessage describes it.
    Ok = 1,          ///< Confirmed: no correction needed.
    Recognized = 2,  ///< The block-recognition reply is the block text — text holds it.
    Fixed = 3,       ///< The fix reply carries a replacement text — text holds it.
    Review = 4,      ///< The block is unreadable — needs human eyes.
    Mismatch = 5,    ///< The decision model rejected the match (below the threshold).
};

struct CheckResult {
    CheckStatus status = CheckStatus::Failed;
    QString text;  ///< Block text for Recognized / Fixed.
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