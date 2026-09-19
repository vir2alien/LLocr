#include "app/CheckController.h"

#include <QFutureWatcher>

#include "app/SettingsStore.h"
#include "models/QwenGeneralModel.h"

namespace llocr {

CheckController::CheckController(SettingsStore &settings,
                                 RuntimeController &runtime,
                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_runtime(runtime)
    , m_model(std::make_unique<QwenGeneralModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        const CheckResult result = m_watcher.future().resultCount() > 0
                                       ? m_watcher.result()
                                       : CheckResult::makeError(tr("No response"));
        emit checkFinished(result.success, result.text, result.errorMessage);
        setBusy(false);
    });
}

QList<RequestParameter> CheckController::requestParameters() const
{
    // The check request parameters live in Settings (check-model window,
    // Request tab).
    return {
        { QStringLiteral("temperature"), 0, RequestValueKind::Number,
          m_settings.checkTemperature(),
          QStringLiteral("Sampling temperature") },
        { QStringLiteral("max_tokens"), 1, RequestValueKind::Number,
          QVariant::fromValue(m_settings.checkMaxTokens()),
          QStringLiteral("Maximum tokens to generate") },
        { QStringLiteral("stream"), 2, RequestValueKind::Boolean,
          m_settings.checkStream(),
          QStringLiteral("Stream tokens as they arrive") },
    };
}

ConnectionConfig CheckController::buildConfig(const ResolvedConnection &conn) const
{
    ConnectionConfig config;
    config.apiKey = conn.apiKey;
    config.baseUrl = conn.baseUrl;
    config.timeoutMs = conn.timeoutMs;
    return config;
}

void CheckController::checkBlock(const QImage &image, const QString &recognizedText,
                                 const QString &prompt)
{
    if (m_busy || image.isNull())
        return;

    setBusy(true);

    m_runtime.ensureConnectionReady(this, [this, image, recognizedText, prompt](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (conn.baseUrl.isEmpty()) {
            emit statusRequested(conn.error.isEmpty()
                                     ? tr("Connection is not configured.")
                                     : conn.error);
            emit checkFinished(false, QString(), conn.error);
            setBusy(false);
            return;
        }

        CheckRequest request;
        request.image = image;
        request.recognizedText = recognizedText;
        request.prompt = prompt;
        request.modelId = conn.modelId;
        request.parameters = requestParameters();

        m_watcher.setFuture(m_model->check(request, buildConfig(conn)));
    });
}

void CheckController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}  // namespace llocr