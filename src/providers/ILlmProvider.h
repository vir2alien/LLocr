#pragma once

#include <QFuture>
#include <QImage>
#include <QString>

#include "core/OcrResult.h"
#include "core/RequestProfile.h"
#include "core/ProviderConfig.h"

namespace llocr {

/**
 * @brief Abstraction over a connection to an LLM capable of OCR.
 */

struct OcrRequest {
    QImage image;
    QString prompt;
    QString modelId;

    // Request-body parameters from the request profile (ordered; see
    // RequestProfile). The provider appends them after model/messages.
    QList<RequestParameter> parameters;
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;

    virtual QFuture<OcrResult> recognize(const OcrRequest &request, const ProviderConfig &config) = 0;

    virtual QString name() const = 0;
};

}  // namespace llocr
