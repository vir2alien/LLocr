#pragma once

#include "models/VisionChatModel.h"

namespace llocr {

// The block-recognition task: one block crop in, the block text out. The
// reply is the transcription — service tokens and reasoning wrappers cleaned;
// an empty reply is the "unreadable" verdict, not an error.
class BlockRecognitionModel : public VisionChatModel
{
    Q_DISABLE_COPY_MOVE(BlockRecognitionModel)

public:
    BlockRecognitionModel() = default;
    ~BlockRecognitionModel() = default;

    QFuture<CheckResult> check(const CheckRequest &request, const ConnectionConfig &config);

protected:
    static QByteArray buildRequestBody(const CheckRequest &request, const QByteArray &imageDataUrl);
    static CheckResult parseResponse(const QByteArray &responseData);
};

}  // namespace llocr
