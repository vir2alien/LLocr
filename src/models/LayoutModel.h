#pragma once

#include "models/VisionChatModel.h"

namespace llocr {

// The layout task: one page (squashed by the controller) in, block markup out.
// The reply is structured markup — its <|...|> tokens are the data a layout
// parser reads, so it reaches the parser without the text cleanup the block
// replies get.
class LayoutModel : public VisionChatModel
{
    Q_DISABLE_COPY_MOVE(LayoutModel)

public:
    LayoutModel() = default;
    ~LayoutModel() = default;

    QFuture<CheckResult> check(const CheckRequest &request, const ConnectionConfig &config);

protected:
    static QByteArray buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);
};

}  // namespace llocr
