#pragma once

#include <QString>

namespace llocr {

struct ConnectionConfig {
    QString baseUrl = QStringLiteral("http://localhost:8080");  ///< Without trailing slash.
    QString apiKey;                                             ///< Optional bearer token.
    // Per-request timeout. Always taken from SettingsStore::connectionTimeoutMs
    // (120 s); the previous 240 s default here contradicted it (ADR 111).
    int timeoutMs = 120000;
};

} // namespace llocr
