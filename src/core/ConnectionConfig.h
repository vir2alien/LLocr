#pragma once

#include <QString>

namespace llocr {

struct ConnectionConfig {
    QString baseUrl = QStringLiteral("http://localhost:8080");  ///< Without trailing slash.
    QString apiKey;                                             ///< Optional bearer token.
    int timeoutMs = 120000;
};

}  // namespace llocr
