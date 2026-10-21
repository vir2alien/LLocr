#pragma once

#include "models/VisionChatModel.h"

namespace llocr {

// The fix task: the block crop plus the OCR candidate under correction. The
// reply contract is OK / FIX / REVIEW; a FIX reply is span edits
// (`old => new` lines) for the text types, the complete corrected block for
// the formula types, and the complete block plus a mandatory EDITS: section
// for the table type — all of them pass the diff gate before they reach the
// block (ADR 179).
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
