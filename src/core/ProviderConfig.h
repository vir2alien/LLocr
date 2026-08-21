#pragma once

#include <QString>

namespace llocr {

struct ProviderConfig {
    // Connection
    QString baseUrl = QStringLiteral("http://localhost:8080");  ///< Without trailing slash.
    QString apiKey;                                             ///< Optional bearer token.
    int timeoutMs = 240000;                                     ///< Per-request timeout.
};

} // namespace llocr
