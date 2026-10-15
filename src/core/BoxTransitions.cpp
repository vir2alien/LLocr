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

}  // namespace

bool hasCorrection(const BoundingBox &box)
{
    return !box.correctedText.isEmpty() || box.correctedCheckStatus != BoxCheckStatus::NotChecked;
}

QString effectiveText(const BoundingBox &box)
{
    return box.correctedText.isEmpty() ? box.text : box.correctedText;
}

BoxCheckStatus effectiveCheckStatus(const BoundingBox &box)
{
    return hasCorrection(box) ? box.correctedCheckStatus : box.checkStatus;
}

double effectiveConfidence(const BoundingBox &box)
{
    return hasCorrection(box) ? box.correctedConfidence : box.confidence;
}

void ocrResultApplied(BoundingBox &box, const QString &text)
{
    box.text = text;
    eraseCorrection(box);
    box.checkStatus = BoxCheckStatus::NotChecked;
    box.confidence = -1.0;
}

void blockUnreadable(BoundingBox &box)
{
    eraseCorrection(box);
    box.checkStatus = BoxCheckStatus::Review;
    box.confidence = -1.0;
}

void verificationStarted(BoundingBox &box)
{
    if (hasCorrection(box)) {
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
    if (hasCorrection(box)) {
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
}

void fixConfirmedOriginal(BoundingBox &box)
{
    box.correctedText = box.text;
    box.correctedCheckStatus = BoxCheckStatus::NotChecked;
    box.correctedConfidence = -1.0;
}

void fixMarkedUnreadable(BoundingBox &box)
{
    eraseCorrection(box);
    box.correctedCheckStatus = BoxCheckStatus::Review;
}

void correctionReverted(BoundingBox &box)
{
    eraseCorrection(box);
}

}  // namespace llocr::BoxTransitions
