#include "core/BoxTransitions.h"

namespace llocr::BoxTransitions {

namespace {

BoxCheckStatus verdictFor(CheckStatus status)
{
    switch (status) {
    case CheckStatus::Ok:
        return BoxCheckStatus::Ok;
    case CheckStatus::Mismatch:
        return BoxCheckStatus::Mismatch;
    case CheckStatus::Review:
        return BoxCheckStatus::Review;
    case CheckStatus::Failed:
    case CheckStatus::Recognized:
    case CheckStatus::Fixed:
        break;
    }
    return BoxCheckStatus::NotChecked;
}

void eraseCorrection(BoundingBox &box)
{
    box.correctedText.clear();
    box.correctedCheckStatus = BoxCheckStatus::NotChecked;
    box.correctedConfidence = -1.0;
}

// The layer a decision run judges and the one the dot follows: the layer the
// user reads. A verdict-only correction layer (a fix REVIEW) speaks for the
// block — there is no corrected text to read, and the damage is the block's
// state.
bool judgedLayerIsCorrection(const BoundingBox &box)
{
    return box.useCorrectedText && hasCorrection(box);
}

}  // namespace

bool hasCorrection(const BoundingBox &box)
{
    return !box.correctedText.isEmpty() || box.correctedCheckStatus != BoxCheckStatus::NotChecked;
}

bool correctionSelected(const BoundingBox &box)
{
    return box.useCorrectedText && !box.correctedText.isEmpty();
}

QString effectiveText(const BoundingBox &box)
{
    return correctionSelected(box) ? box.correctedText : box.text;
}

BoxCheckStatus effectiveCheckStatus(const BoundingBox &box)
{
    return judgedLayerIsCorrection(box) ? box.correctedCheckStatus : box.checkStatus;
}

double effectiveConfidence(const BoundingBox &box)
{
    return judgedLayerIsCorrection(box) ? box.correctedConfidence : box.confidence;
}

void ocrResultApplied(BoundingBox &box, const QString &text)
{
    box.text = text;
    eraseCorrection(box);
    box.checkStatus = BoxCheckStatus::NotChecked;
    box.confidence = -1.0;
    box.fixStatus = BoxFixStatus::None;
    box.useCorrectedText = true;
}

void blockUnreadable(BoundingBox &box)
{
    eraseCorrection(box);
    box.checkStatus = BoxCheckStatus::Review;
    box.confidence = -1.0;
    box.fixStatus = BoxFixStatus::None;
}

void verificationStarted(BoundingBox &box)
{
    if (judgedLayerIsCorrection(box)) {
        box.correctedCheckStatus = BoxCheckStatus::NotChecked;
        box.correctedConfidence = -1.0;
    } else {
        box.checkStatus = BoxCheckStatus::NotChecked;
        box.confidence = -1.0;
    }
}

void verdictApplied(BoundingBox &box, const CheckResult &result)
{
    const BoxCheckStatus status = verdictFor(result.status);
    if (judgedLayerIsCorrection(box)) {
        box.correctedCheckStatus = status;
        box.correctedConfidence = result.confidence;
    } else {
        box.checkStatus = status;
        box.confidence = result.confidence;
    }
}

void fixApplied(BoundingBox &box, const QString &text)
{
    box.correctedText = text;
    box.correctedCheckStatus = BoxCheckStatus::NotChecked;
    box.correctedConfidence = -1.0;
    box.fixStatus = BoxFixStatus::Fixed;
    // A fresh correction is the text the user wants to read (ADR 185): the
    // panel switches to it.
    box.useCorrectedText = true;
}

void fixConfirmedOriginal(BoundingBox &box)
{
    if (correctionSelected(box)) {
        // The candidate was a stored correction: it is confirmed as it is.
        box.correctedCheckStatus = BoxCheckStatus::Ok;
        box.correctedConfidence = -1.0;
    } else {
        eraseCorrection(box);
    }
    box.fixStatus = BoxFixStatus::NotNeeded;
    // The text layer keeps its verdict (ADR 185): the fixer confirmed the
    // text, the decision's verdict about it is still the verdict.
}

void fixMarkedUnreadable(BoundingBox &box)
{
    eraseCorrection(box);
    box.correctedCheckStatus = BoxCheckStatus::Review;
    box.fixStatus = BoxFixStatus::Damaged;
    // The damage belongs to the block: the verdict must show even when the
    // user was reading the text layer.
    box.useCorrectedText = true;
}

void correctionReverted(BoundingBox &box)
{
    eraseCorrection(box);
    box.fixStatus = BoxFixStatus::None;
}

void blockApproved(BoundingBox &box)
{
    if (correctionSelected(box)) {
        box.correctedCheckStatus = BoxCheckStatus::Approved;
    } else {
        box.checkStatus = BoxCheckStatus::Approved;
    }
}

void layerSwitched(BoundingBox &box, bool corrected)
{
    box.useCorrectedText = corrected;
}

}  // namespace llocr::BoxTransitions
