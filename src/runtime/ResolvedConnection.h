#pragma once

#include <QString>

namespace llocr {

// Result of resolving a usable connection from the current mode. In `External`
// this is derived immediately from SettingsStore; in `Managed` the controller
// starts the server, waits for /health, and computes baseUrl/modelId itself.
struct ResolvedConnection {
    QString baseUrl;    // http://127.0.0.1:<port> or the external URL
    QString apiKey;     // from settings (External) or empty (Managed)
    QString modelId;    // alias (Managed) or model/name (External)
    int timeoutMs = 0;

    // When baseUrl is empty the resolve failed. This carries a human-readable
    // message from the §7.5 error matrix (or a generic one) so the recognition
    // path can surface it verbatim instead of a generic "not configured".
    QString error;
};

// Outcome of RuntimeController::runSelfTest(): one real OCR request against the
// running (or just started) server, using a built-in synthetic test image.
struct SelfTestResult {
    bool ok = false;
    QString text;    // OCR text returned by the server on success
    QString error;    // human-readable failure from the §7.5 error matrix
};

}  // namespace llocr