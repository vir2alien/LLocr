#pragma once

#include <QList>
#include <QString>

#include "core/CheckResult.h"
#include "core/StatusMessage.h"

namespace llocr::FixDiffGate {

// Deterministic gate over a FIX reply (ADR 179). Span replies never carry
// unchanged text — the candidate is edited here, so the model cannot corrupt
// what it was not asked to change. Full-block replies (table/equation) must
// declare every change under EDITS:, and a word-level diff demotes any
// undeclared edit to Review. The equivalence normalization mirrors the block
// prompts' "Equivalent (never fix)" lists, so a cosmetic fix reads as a no-op
// and keeps the original candidate.
struct Edit {
    QString oldText;
    QString newText;
    QString location;  // "r2c3" / "structure" on table replies, empty otherwise
};

struct Outcome {
    CheckStatus status = CheckStatus::Failed;
    QString text;                   // Fixed: the corrected text; Ok/Review: unused
    StatusMessage downgradeReason;  // why a FIX reply became Ok or Review
};

// The structural types whose FIX reply carries the whole block plus EDITS:.
bool isFullBlockType(const QString &blockType);

// Equation/formula: LaTeX spacing on the wire is normalized and the math
// comparisons run space-insensitively.
bool isMathBlockType(const QString &blockType);

// Parses the reply body (everything after the FIX keyword) as `old => new`
// lines and applies them to the candidate.
Outcome fixSpan(const QString &candidate, const QString &body, const QString &blockType);

// Splits the reply body into the corrected block and the EDITS: section, then
// verifies the block against the candidate.
Outcome fixFullBlock(const QString &candidate, const QString &body, const QString &blockType);

// NFC, soft hyphens, ligatures, ellipsis; spaces and quotes collapse except in
// code. Strictly-different pairs (е/ё, homoglyphs, …) are never normalized.
QString normalizeForEquivalence(const QString &text, bool isCode);

// Canonical wire form of a LaTeX candidate ("Bi _ {1}" -> "Bi_{1}",
// "\\frac {...}" -> "\\frac{...}"): the OCR models emit spaced LaTeX that
// provokes the fixer into pure spacing churn (ADR 180). Rendering-preserving.
QString normalizeLatexCandidate(const QString &text);

}  // namespace llocr::FixDiffGate
