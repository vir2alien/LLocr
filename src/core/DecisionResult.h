#pragma once

#include <QString>

#include "core/CheckResult.h"
#include "core/StatusMessage.h"

namespace llocr {

struct DecisionResult {
    bool ok = false;           ///< false = request/parse error; errorMessage describes it.
    double probability = 0.0;  ///< P(match) — read straight off the model head, no decode phase.
    StatusMessage errorMessage;

    static DecisionResult makeError(const StatusMessage &message)
    {
        DecisionResult result;
        result.ok = false;
        result.errorMessage = message;
        return result;
    }

    // The visual-diff protocol answers only `match`, so the verdict is the
    // threshold on P(match) — Ok above it, Mismatch below. A crop the model
    // cannot read lands on Mismatch and goes to the fixer, which answers
    // REVIEW for it (docs/31); the text protocol's separate Review routes are
    // gone with it.
    CheckStatus routedStatus(double matchThreshold) const { return probability >= matchThreshold ? CheckStatus::Ok : CheckStatus::Mismatch; }
};

}  // namespace llocr
