#pragma once

#include <QString>

namespace llocr {

enum class CheckStatus : int {
    Failed = 0,  ///< Network/parse error — errorMessage describes it.
    Ok = 1,      ///< The verifier returned "OK" — the recognized text is correct.
    Fixed = 2,   ///< The verifier returned "FIX\n…" — text holds the corrected block.
    Review = 3,  ///< The verifier returned "REVIEW" — the block is not readable.
};

struct CheckResult {
    CheckStatus status = CheckStatus::Failed;
    QString text;            ///< Corrected block text when status == Fixed.
    QString errorMessage;    ///< Human-readable error when status == Failed.

    static CheckResult makeError(const QString& message) {
        CheckResult result;
        result.status = CheckStatus::Failed;
        result.errorMessage = message;
        return result;
    }
};

}  // namespace llocr