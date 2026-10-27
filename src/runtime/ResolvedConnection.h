#pragma once

#include <QString>

#include "core/ConnectionConfig.h"
#include "core/ConnectionRole.h"

namespace llocr {

struct ResolvedConnection {
    QString baseUrl;  // http://127.0.0.1:<port> or the external URL
    QString apiKey;   // from settings (External) or empty (Managed)
    QString modelId;  // alias (Managed) or model/name / check/modelName / decision/modelName / layout/modelName (External, per ConnectionRole)
    int connectionTimeoutMs = 0;
    int responseTimeoutMs = 0;
    int contextSize = 0;  // the server's context window; 0 = unknown

    QString error;

    ConnectionConfig toConnectionConfig() const
    {
        ConnectionConfig config;
        config.apiKey = apiKey;
        config.baseUrl = baseUrl;
        config.modelId = modelId;
        config.connectionTimeoutMs = connectionTimeoutMs;
        config.responseTimeoutMs = responseTimeoutMs;
        return config;
    }
};

struct SelfTestResult {
    bool ok = false;
    QString text;   // OCR text returned by the server on success
    QString error;  // human-readable failure from the §7.5 error matrix
};

}  // namespace llocr
