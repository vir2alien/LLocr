#pragma once

#include <QFuture>
#include <QImage>
#include <QString>

#include "core/OcrResult.h"
#include "core/ProviderConfig.h"

namespace llocr {

/**
 * @brief Abstraction over a connection to an LLM capable of OCR.
 */

struct OcrRequest {
    QImage image;
    QString prompt;
    QString modelId;

    // Optional
    double temperature = 0.0;
    int maxTokens = 4096;

    // Sampling parameters (hardcoded for now; to be exposed in Settings later)
    double repeatPenalty = 1.5;  // 1.5 for dense pages, 1.3 for normal
    int repeatLastN = 512;
    double dryMultiplier = 1.0;
    double dryBase = 1.75;
    int dryAllowedLength = 2;  // 2 for dense pages, 4 for normal
    int dryPenaltyLastN = -1;
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;

    virtual QFuture<OcrResult> recognize(const OcrRequest &request, const ProviderConfig &config) = 0;

    virtual QString name() const = 0;
};

}  // namespace llocr
