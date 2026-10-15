#pragma once

#include "core/CheckResult.h"
#include "core/OcrResult.h"

namespace llocr::BoxTransitions {

// The correction layer exists when it carries text or a verdict — a fix
// REVIEW stores a verdict without text, and the dot must still show damaged.
bool hasCorrection(const BoundingBox &box);

// What the user reads, verifies and exports: the correction when the layer
// has text, else the OCR text.
QString effectiveText(const BoundingBox &box);
BoxCheckStatus effectiveCheckStatus(const BoundingBox &box);
double effectiveConfidence(const BoundingBox &box);

// Any OCR result (page OCR, block recognition, re-recognition) lands in the
// text layer and erases the correction layer entirely.
void ocrResultApplied(BoundingBox &box, const QString &text);

// The transcription model answered "unreadable": the block keeps its text
// but the text layer carries the Review verdict.
void blockUnreadable(BoundingBox &box);

// A decision run starts: the effective layer's previous verdict is erased.
void verificationStarted(BoundingBox &box);

// The decision verdict (and its confidence, when carried) lands on the
// effective layer.
void verdictApplied(BoundingBox &box, const CheckResult &result);

// The fix protocol's outcomes (ADR 163): a correction replaces the layer's
// text, an OK stores the text as a duplicate, a REVIEW marks the block
// damaged without storing a correction. The text layer's verdict survives
// all three — it is about the OCR text, not the correction.
void fixApplied(BoundingBox &box, const QString &text);
void fixConfirmedOriginal(BoundingBox &box);
void fixMarkedUnreadable(BoundingBox &box);

// «Отменить исправление»: the block rolls back to the text layer's state.
void correctionReverted(BoundingBox &box);

}  // namespace llocr::BoxTransitions
