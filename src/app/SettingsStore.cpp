#include "app/SettingsStore.h"

#include <QSettings>

namespace llocr {

SettingsStore::SettingsStore()
{
}

ProviderConfig SettingsStore::load() const
{
    QSettings settings;
    ProviderConfig config;

    config.baseUrl = settings.value(kBaseUrl, QStringLiteral("http://localhost:8080")).toString();
    config.apiKey = settings.value(kApiKey, QString{}).toString();
    config.timeoutMs = settings.value(kTimeoutMs, 120000).toInt();

    config.modelName = settings.value(kModelName, "Unlimited-OCR").toString();
    config.temperature = settings.value(kTemperature, 0).toDouble();
    config.maxTokens = settings.value(kMaxTokens, 32768).toInt();

    config.parserId = settings.value(kParserId, "raw").toString();
    config.bboxCoordinateRange = settings.value(kBboxRange, 1000).toInt();

    return config;
}

void SettingsStore::save(const ProviderConfig &config) const
{
    QSettings settings;
    settings.setValue(kBaseUrl, config.baseUrl);
    settings.setValue(kApiKey, config.apiKey);
    settings.setValue(kTimeoutMs, config.timeoutMs);
    settings.setValue(kModelName, config.modelName);
    settings.setValue(kTemperature, config.temperature);
    settings.setValue(kMaxTokens, config.maxTokens);
    settings.setValue(kParserId, config.parserId);
    settings.setValue(kBboxRange, config.bboxCoordinateRange);
    settings.sync();
}

} // namespace llocr
