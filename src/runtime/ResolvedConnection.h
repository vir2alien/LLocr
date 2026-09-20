#pragma once

#include <QString>

namespace llocr {

enum class ConnectionRole { Ocr, Check, };

struct ResolvedConnection {
    QString baseUrl;    // http://127.0.0.1:<port> or the external URL
    QString apiKey;     // from settings (External) or empty (Managed)
    QString modelId;    // alias (Managed) or model/name / check/modelName (External, per ConnectionRole)
    int timeoutMs = 0;

    QString error;
};

struct SelfTestResult {
    bool ok = false;
    QString text;    // OCR text returned by the server on success
    QString error;    // human-readable failure from the §7.5 error matrix
};

}  // namespace llocr