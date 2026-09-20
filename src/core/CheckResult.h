#pragma once

#include <QString>

namespace llocr {

struct CheckResult {
    bool success = false;
    QString text;            ///< Corrected text when success == true.
    QString errorMessage;    ///< Human-readable error when success == false.

    static CheckResult makeError(const QString& message) {
        CheckResult result;
        result.success = false;
        result.errorMessage = message;
        return result;
    }
};

}  // namespace llocr
