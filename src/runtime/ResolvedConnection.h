#pragma once

#include <QString>

namespace llocr {

// Result of resolving a usable connection from the current mode. In `External`
// this is derived immediately from SettingsStore; in `Managed` the controller
// starts the server, waits for /health, and computes baseUrl/modelId itself.
struct ResolvedConnection {
    QString baseUrl;    // http://127.0.0.1:<port> or the external URL
    QString apiKey;      // from settings (External) or empty (Managed)
    QString modelId;     // alias (Managed) or model/name (External)
    int timeoutMs;
};

}  // namespace llocr