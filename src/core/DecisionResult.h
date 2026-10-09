#pragma once

#include <QString>

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
};

}  // namespace llocr
