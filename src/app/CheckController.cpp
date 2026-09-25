#include "app/CheckController.h"

#include <QFutureWatcher>

#include "app/RequestProfileStore.h"
#include "models/QwenGeneralModel.h"

namespace llocr {

CheckController::CheckController(RequestProfileStore &requestProfiles,
                                 RuntimeController &runtime,
                                 QObject *parent)
    : QObject(parent)
    , m_requestProfiles(requestProfiles)
    , m_runtime(runtime)
    , m_model(std::make_unique<QwenGeneralModel>())
{
    connect(&m_watcher, &QFutureWatcher<CheckResult>::finished, this, [this]() {
        if (m_stopRequested) {
            emit statusRequested(tr("Check stopped."));
            setBusy(false);
            return;
        }
        const CheckResult result = m_watcher.future().resultCount() > 0
                                       ? m_watcher.result()
                                       : CheckResult::makeError(tr("No response"));
        emit checkFinished(result);
        setBusy(false);
    });
}

void CheckController::stop()
{
    if (!m_busy)
        return;
    m_stopRequested = true;
    if (m_model)
        m_model->abort();
    m_runtime.cancelPendingStart();
    emit statusRequested(tr("Stopping\u2026"));
}

QList<RequestParameter> CheckController::requestParameters() const
{
    return m_requestProfiles.activeProfile().parameters;
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
                                 const QString &systemPrompt, const QString &typePrompt)
{
    if (m_busy || image.isNull())
        return;

    setBusy(true);
    m_stopRequested = false;

    m_runtime.ensureConnectionReady(this, ConnectionRole::Check,
                                    [this, image, recognizedText, systemPrompt, typePrompt](const ResolvedConnection &conn) {
        if (!m_busy)
            return;  // stopped while resolving
        if (m_stopRequested) {
            emit statusRequested(tr("Stopped before check started."));
            setBusy(false);
            return;
        }
        if (conn.baseUrl.isEmpty()) {
            const QString message = conn.error.isEmpty()
                ? tr("Connection is not configured.")
                : conn.error;
            emit statusRequested(message);
            emit checkFinished(CheckResult::makeError(message));
            setBusy(false);
            return;
        }

        CheckRequest request;
        request.image = image;
        request.recognizedText = recognizedText;
        request.systemPrompt = systemPrompt;
        request.typePrompt = typePrompt;
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
