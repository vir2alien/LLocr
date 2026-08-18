#pragma once

#include <QString>

namespace llocr {

struct ProviderConfig {
    // Connection
    QString baseUrl = QStringLiteral("http://localhost:8080");  ///< Without trailing slash.
    QString apiKey;                                             ///< Optional bearer token.
    int timeoutMs = 240000;                                     ///< Per-request timeout.

    // Model
    QString modelName = QStringLiteral("gpt-4o-mini");  ///< Sent as "model".
    QString prompt = QStringLiteral("OCR this document. Return the text.");
    double temperature = 0.0;  ///< Sampling temperature.
    int maxTokens = 16384;     ///< Response token cap.

    // Parser
    QString parserId = QStringLiteral("raw");                   ///< "raw", "det_tokens", …
    // int bboxCoordinateRange = 1000;                            ///< Raw coord scale (0..this).
};

} // namespace llocr
