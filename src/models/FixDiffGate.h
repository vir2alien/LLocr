#pragma once

#include <QString>

#include "core/CheckResult.h"
#include "core/StatusMessage.h"

namespace llocr::FixDiffGate {

// The gate over a FIX reply (ADR 192). The reply carries the complete
// corrected block, so the gate decides one thing only: may that block replace
// the candidate? A block equivalent to the candidate keeps the candidate's own
// bytes (the model re-spelled it, changed nothing); an empty reply goes to
// human eyes. The equivalence normalization mirrors the block prompts'
// "Never fix" lists, so a cosmetic rewrite reads as a no-op.
struct Outcome {
    CheckStatus status = CheckStatus::Failed;
    QString text;                   // Fixed: the corrected block
    StatusMessage downgradeReason;  // why a FIX reply became Ok or Review
};

// Equation/formula: LaTeX spacing on the wire is normalized and the math
// comparisons run space-insensitively.
bool isMathBlockType(const QString &blockType);

// Reads the reply body as the complete corrected block: Fixed when it differs
// from the candidate, Ok when it is equivalent to it, Review when the reply
// carries nothing to apply.
Outcome fixBlock(const QString &candidate, const QString &body, const QString &blockType);

// NFC, soft hyphens, ligatures, ellipsis; spaces and line breaks collapse
// except in code. Strictly-different pairs (е/ё, homoglyphs, …) are never
// normalized.
QString normalizeForEquivalence(const QString &text, bool isCode);

// Canonical wire form of a LaTeX candidate ("Bi _ {1}" -> "Bi_{1}",
// "\\frac {...}" -> "\\frac{...}"): the OCR models emit spaced LaTeX that
// provokes the fixer into pure spacing churn (ADR 180). Rendering-preserving.
QString normalizeLatexCandidate(const QString &text);

}  // namespace llocr::FixDiffGate
