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
    box.correctedCheckStatusBeforeApproval = BoxCheckStatus::NotChecked;
}

// The layer a decision run judges and the one the dot follows: the layer the
// user reads. A verdict-only correction layer (a fix REVIEW) speaks for the
// block — there is no corrected text to read, and the damage is the block's
// state.
bool judgedLayerIsCorrection(const BoundingBox &box)
{
    return box.useCorrectedText && hasCorrection(box);
}

// The verdict the approval replaced is forgotten: the layer carries a new
// verdict now, and the old one must not come back on a revert.
void forgetApproval(BoundingBox &box, bool correctedLayer)
{
    if (correctedLayer)
        box.correctedCheckStatusBeforeApproval = BoxCheckStatus::NotChecked;
    else
        box.checkStatusBeforeApproval = BoxCheckStatus::NotChecked;
}

}  // namespace

bool hasCorrection(const BoundingBox &box)
{
    return !box.correctedText.isEmpty() || box.correctedCheckStatus != BoxCheckStatus::NotChecked;
}

bool effectiveLayerIsCorrection(const BoundingBox &box)
{
    return judgedLayerIsCorrection(box);
}

bool correctionSelected(const BoundingBox &box)
{
    return box.useCorrectedText && !box.correctedText.isEmpty();
}

bool needsFix(const BoundingBox &box)
{
    const BoxCheckStatus status = effectiveCheckStatus(box);
    if (status != BoxCheckStatus::Mismatch && status != BoxCheckStatus::Review)
        return false;
    // The fixer has answered this candidate: it confirmed the text or it could
    // not read it. The block waits for the operator now.
    return box.fixStatus != BoxFixStatus::NotNeeded && box.fixStatus != BoxFixStatus::Damaged;
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
    forgetApproval(box, false);
}

void blockUnreadable(BoundingBox &box)
{
    eraseCorrection(box);
    box.checkStatus = BoxCheckStatus::Review;
    box.confidence = -1.0;
    box.fixStatus = BoxFixStatus::None;
    forgetApproval(box, false);
}

void verificationStarted(BoundingBox &box)
{
    if (judgedLayerIsCorrection(box)) {
        box.correctedCheckStatus = BoxCheckStatus::NotChecked;
        box.correctedConfidence = -1.0;
        forgetApproval(box, true);
    } else {
        box.checkStatus = BoxCheckStatus::NotChecked;
        box.confidence = -1.0;
        forgetApproval(box, false);
    }
}

void verdictApplied(BoundingBox &box, const CheckResult &result)
{
    const BoxCheckStatus status = verdictFor(result.status);
    if (judgedLayerIsCorrection(box)) {
        box.correctedCheckStatus = status;
        box.correctedConfidence = result.confidence;
        forgetApproval(box, true);
    } else {
        box.checkStatus = status;
        box.confidence = result.confidence;
        forgetApproval(box, false);
    }
}

void fixApplied(BoundingBox &box, const QString &text)
{
    box.correctedText = text;
    box.correctedCheckStatus = BoxCheckStatus::NotChecked;
    box.correctedConfidence = -1.0;
    box.fixStatus = BoxFixStatus::Fixed;
    forgetApproval(box, true);
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
        forgetApproval(box, true);
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
    // The model's verdict is remembered: the approval is the user's
    // confirmation, and undoing it must bring the verdict back (ADR 187).
    if (effectiveLayerIsCorrection(box)) {
        if (box.correctedCheckStatus != BoxCheckStatus::Approved)
            box.correctedCheckStatusBeforeApproval = box.correctedCheckStatus;
        box.correctedCheckStatus = BoxCheckStatus::Approved;
    } else {
        if (box.checkStatus != BoxCheckStatus::Approved)
            box.checkStatusBeforeApproval = box.checkStatus;
        box.checkStatus = BoxCheckStatus::Approved;
    }
}

void blockApprovalReverted(BoundingBox &box)
{
    // The confirmation is undone: the verdict the model gave stands again.
    // Only an approved layer has something to undo — a second revert must
    // not wipe the verdict the first one restored.
    if (effectiveLayerIsCorrection(box)) {
        if (box.correctedCheckStatus != BoxCheckStatus::Approved)
            return;
        box.correctedCheckStatus = box.correctedCheckStatusBeforeApproval;
        box.correctedCheckStatusBeforeApproval = BoxCheckStatus::NotChecked;
    } else {
        if (box.checkStatus != BoxCheckStatus::Approved)
            return;
        box.checkStatus = box.checkStatusBeforeApproval;
        box.checkStatusBeforeApproval = BoxCheckStatus::NotChecked;
    }
}

void layerSwitched(BoundingBox &box, bool corrected)
{
    box.useCorrectedText = corrected;
}

void blockTextEdited(BoundingBox &box, const QString &text, bool correctedLayer)
{
    // Nothing was written: the verdict the model gave stands — a version the
    // user opened and left alone is not a hand-written one.
    if (correctedLayer ? box.correctedText == text : box.text == text)
        return;

    // The user writes into one of the two versions; the other one keeps its
    // own state. The hand-written text is the user's own, not the model's and
    // not a confirmation of it (ADR 187): the layer carries «Изменён
    // вручную». The version in view follows the hand: the text the user just
    // wrote is the text the document must carry.
    if (correctedLayer) {
        box.correctedText = text;
        box.correctedCheckStatus = BoxCheckStatus::Edited;
        box.correctedConfidence = -1.0;
    } else {
        box.text = text;
        box.checkStatus = BoxCheckStatus::Edited;
        box.confidence = -1.0;
    }
    box.useCorrectedText = correctedLayer;
    // The hand-written text is a new candidate: the fixer's old answer is
    // about another text and must not silence the block (ADR 188).
    box.fixStatus = BoxFixStatus::None;
}

}  // namespace llocr::BoxTransitions
