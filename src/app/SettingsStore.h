#pragma once

#include <QString>

#include "core/ProviderConfig.h"

namespace llocr {

class SettingsStore {
public:
    SettingsStore();

    ProviderConfig load() const;
    void save(const ProviderConfig& config) const;

private:
    // Connection
    static constexpr const char* kBaseUrl = "provider/baseUrl";
    static constexpr const char* kApiKey = "provider/apiKey";
    static constexpr const char* kTimeoutMs = "provider/timeoutMs";

    // Model
    static constexpr const char* kModelName = "model/name";
    static constexpr const char* kPrompt = "model/prompt";
    static constexpr const char* kTemperature = "model/temperature";
    static constexpr const char* kMaxTokens = "model/maxTokens";

    // Output / parser
    static constexpr const char* kParserId = "output/parser";
    static constexpr const char* kBboxRange = "output/bboxCoordinateRange";
};

} // namespace llocr
