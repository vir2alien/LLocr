#pragma once

#include "models/VisionChatModel.h"

namespace llocr {

// The fix task: the block crop plus the OCR candidate under correction. The
// reply contract is OK / FIX / REVIEW; a FIX reply carries the complete
// corrected block, and the gate (ADR 192) decides whether that block may
// replace the candidate — an equivalent block keeps the candidate's bytes, an
// empty or budget-cut reply goes to human eyes.
class FixModel : public VisionChatModel
{
    Q_DISABLE_COPY_MOVE(FixModel)

public:
    FixModel() = default;
    ~FixModel() = default;

    QFuture<CheckResult> check(const CheckRequest &request, const ConnectionConfig &config);

protected:
    static QByteArray buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData, const QString &candidate, const QString &blockType);
};

}  // namespace llocr
