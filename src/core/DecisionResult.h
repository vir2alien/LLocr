#pragma once

#include <QHash>
#include <QString>

#include "core/CheckResult.h"
#include "core/StatusMessage.h"

namespace llocr {

struct DecisionResult {
    bool ok = false;                   ///< false = request/parse error; errorMessage describes it.
    double probability = 0.0;          ///< P(match) — read straight off the model head, no decode phase.
    double hasText = -1.0;             ///< P(readable text on the crop); <0 = not asked (scheme v5).
    double readable = -1.0;            ///< P(crop legible where it must be compared); <0 = not asked.
    QHash<QString, double> diagnosis;  ///< choice-class probabilities; empty = not asked.
    StatusMessage errorMessage;

    static DecisionResult makeError(const StatusMessage &message)
    {
        DecisionResult result;
        result.ok = false;
        result.errorMessage = message;
        return result;
    }

    QString topDiagnosis() const
    {
        QString top;
        double best = -1.0;
        for (auto it = diagnosis.constBegin(); it != diagnosis.constEnd(); ++it) {
            if (it.value() > best) {
                best = it.value();
                top = it.key();
            }
        }
        return top;
    }

    // Routing (ADR 183): the decision pass asks ONE question, so the verdict
    // is the threshold on P(match) — Ok above it, Mismatch below. The aux
    // answers (has_text, readable, the diagnosis classes) are still read when
    // a server sends them, and they keep their routes: an empty or unreadable
    // crop goes to Review (human eyes, never a fixer call). EMPTY and REVIEW
    // both land on the existing Review status — no new block states.
    CheckStatus routedStatus(double matchThreshold, double emptyThreshold, double readableThreshold) const
    {
        const QString top = topDiagnosis();
        if (hasText >= 0 && (hasText < emptyThreshold || top == QLatin1String("empty_crop")))
            return CheckStatus::Review;
        if (readable >= 0 && (readable < readableThreshold || top == QLatin1String("unreadable_or_clipped")))
            return CheckStatus::Review;
        const bool diagAgrees = top.isEmpty() || top == QLatin1String("exact_match");
        return probability >= matchThreshold && diagAgrees ? CheckStatus::Ok : CheckStatus::Mismatch;
    }
};

}  // namespace llocr
