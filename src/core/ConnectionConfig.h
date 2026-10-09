#pragma once

#include <QString>

namespace llocr {

struct ConnectionConfig {
    QString baseUrl = QStringLiteral("http://localhost:8080");  ///< Without trailing slash.
    QString apiKey;                                             ///< Optional bearer token.
    int connectionTimeoutMs = 5000;                             ///< TCP connect phase only.
    int responseTimeoutMs = 300000;                             ///< Whole request, headers to last byte.
};

}  // namespace llocr
