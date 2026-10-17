#pragma once

#include "models/VisionChatModel.h"

namespace llocr {

// The fix task: the block crop plus the OCR candidate under correction, and a
// reply contract of exactly OK / FIX / REVIEW (ADR 163).
class FixModel : public VisionChatModel
{
    Q_DISABLE_COPY_MOVE(FixModel)

public:
    FixModel() = default;
    ~FixModel() = default;

    QFuture<CheckResult> check(const CheckRequest &request, const ConnectionConfig &config);

protected:
    static QByteArray buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);
};

}  // namespace llocr
